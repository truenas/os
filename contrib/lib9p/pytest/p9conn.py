#! /usr/bin/env python

"""
handle plan9 server <-> client connections

(We can act as either server or client.)

This code needs some doctests or other unit tests...
"""

import errno
import logging
import os
import socket
import struct
import sys
import threading
import time

import lerrno
import numalloc
import p9err
import protocol

qt2n = protocol.qid_type2name

STD_P9_PORT=564

class P9Error(Exception):
    pass

class RemoteError(P9Error):
    """
    Used when the remote returns an error.  We track the client
    (connection instance), the operation being attempted, the
    message, and an error number and type.  The message may be
    from the Rerror reply, or from converting the errno in a dot-L
    or dot-u Rerror reply.  The error number may be None if the
    type is 'Rerror' rather than 'Rlerror'.  The message may be
    None or empty string if a non-None errno supplies the error
    instead.
    """
    def __init__(self, client, op, msg, etype, errno):
        self.client = str(client)
        self.op = op
        self.msg = msg
        self.etype = etype # 'Rerror' or 'Rlerror'
        self.errno = errno # may be None
        self.message = self._get_message()
        super(RemoteError, self).__init__(self, self.message)

    def __repr__(self):
        return ('{0!r}({1}, {2}, {3}, {4}, '
                '{5})'.format(self.__class__.__name__, self.client, self.op,
                              self.msg, self.errno, self.etype))
    def __str__(self):
        prefix = '{0}: {1}: '.format(self.client, self.op)
        if self.errno: # check for "is not None", or just non-false-y?
            name = {'Rerror': '.u', 'Rlerror': 'Linux'}[self.etype]
            middle = '[{0} error {1}] '.format(name, self.errno)
        else:
            middle = ''
        return '{0}{1}{2}'.format(prefix, middle, self.message)

    def is_ENOTSUP(self):
        if self.etype == 'Rlerror':
            return self.errno == lerrno.ENOTSUP
        return self.errno == errno.EOPNOTSUPP

    def _get_message(self):
        "get message based on self.msg or self.errno"
        if self.errno is not None:
            return {
                'Rlerror': p9err.dotl_strerror,
                'Rerror' : p9err.dotu_strerror,
            }[self.etype](self.errno)
        return self.msg

class LocalError(P9Error):
    pass

class TEError(LocalError):
    pass

class P9SockIO(object):
    """
    Common base for server and client, handle send and
    receive to communications channel.  Note that this
    need not set up the channel initially, only the logger.
    The channel is typically connected later.  However, you
    can provide one initially.
    """
    def __init__(self, logger, name=None, server=None, port=STD_P9_PORT):
        self.logger = logger
        self.channel = None
        self.name = name
        self.maxio = None
        self.size_coder = struct.Struct('<I')
        if server is not None:
            self.connect(server, port)
        self.max_payload = 2**32 - self.size_coder.size

    def __str__(self):
        if self.name:
            return self.name
        return repr(self)

    def get_recommended_maxio(self):
        "suggest a max I/O size, for when self.maxio is 0 / unset"
        return 16 * 4096

    def min_maxio(self):
        "return a minimum size below which we refuse to work"
        return self.size_coder.size + 100

    def connect(self, server, port=STD_P9_PORT):
        """
        Connect to given server name / IP address.

        If self.name was none, sets self.name to ip:port on success.
        """
        if self.is_connected():
            raise LocalError('already connected')
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM, 0)
        sock.connect((server, port))
        if self.name is None:
            if port == STD_P9_PORT:
                name = server
            else:
                name = '{0}:{1}'.format(server, port)
        else:
            name = None
        self.declare_connected(sock, name, None)

    def is_connected(self):
        "predicate: are we connected?"
        return self.channel != None

    def declare_connected(self, chan, name, maxio):
        """
        Now available for normal protocol (size-prefixed) I/O.
        
        Replaces chan and name and adjusts maxio, if those
        parameters are not None.
        """
        if maxio:
            minio = self.min_maxio()
            if maxio < minio:
                raise LocalError('maxio={0} < minimum {1}'.format(maxio, minio))
        if chan is not None:
            self.channel = chan
        if name is not None:
            self.name = name
        if maxio is not None:
            self.maxio = maxio
            self.max_payload = maxio - self.size_coder.size

    def reduce_maxio(self, maxio):
        "Reduce maximum I/O size per other-side request"
        minio = self.min_maxio()
        if maxio < minio:
            raise LocalError('new maxio={0} < minimum {1}'.format(maxio, minio))
        if maxio > self.maxio:
            raise LocalError('new maxio={0} > current {1}'.format(maxio,
                                                                  self.maxio))
        self.maxio = maxio
        self.max_payload = maxio - self.size_coder.size

    def declare_disconnected(self):
        "Declare comm channel dead (note: leaves self.name set!)"
        self.channel = None
        self.maxio = None

    def shutwrite(self):
        "Do a SHUT_WR on the outbound channel - can't send more"
        chan = self.channel
        # we're racing other threads here
        try:
            chan.shutdown(socket.SHUT_WR)
        except (OSError, AttributeError):
            pass

    def shutdown(self):
        "Shut down comm channel"
        if self.channel:
            try:
                self.channel.shutdown(socket.SHUT_RDWR)
            except socket.error:
                pass
            self.channel.close()
            self.declare_disconnected()

    def read(self):
        """
        Try to read a complete packet.

        Returns '' for EOF, as read() usually does.

        If we can't even get the size, this still returns ''.
        If we get a sensible size but are missing some data,
        we can return a short packet.  Since we know if we did
        this, we also return a boolean: True means "really got a
        complete packet."

        Note that '' EOF always returns False: EOF is never a
        complete packet.
        """
        if self.channel is None:
            return b'', False
        size_field = self.xread(self.size_coder.size)
        if len(size_field) < self.size_coder.size:
            if len(size_field) == 0:
                self.logger.log(logging.INFO, '%s: normal EOF', self)
            else:
                self.logger.log(logging.ERROR,
                               '%s: EOF while reading size (got %d bytes)',
                               self, len(size_field))
                # should we raise an error here?
            return b'', False

        size = self.size_coder.unpack(size_field)[0] - self.size_coder.size
        if size <= 0 or size > self.max_payload:
            self.logger.log(logging.ERROR,
                            '%s: incoming size %d is insane '
                            '(max payload is %d)',
                            self, size, self.max_payload)
            # indicate EOF - should we raise an error instead, here?
            return b'', False
        data = self.xread(size)
        return data, len(data) == size

    def xread(self, nbytes):
        """
        Read nbytes bytes, looping if necessary.  Return '' for
        EOF; may return a short count if we get some data, then
        EOF.
        """
        assert nbytes > 0
        # Try to get everything at once (should usually succeed).
        # Return immediately for EOF or got-all-data.
        data = self.channel.recv(nbytes)
        if data == b'' or len(data) == nbytes:
            return data

        # Gather data fragments into an array, then join it all at
        # the end.
        count = len(data)
        data = [data]
        while count < nbytes:
            more = self.channel.recv(nbytes - count)
            if more == b'':
                break
            count += len(more)
            data.append(more)
        return b''.join(data)

    def write(self, data):
        """
        Write all the data, in the usual encoding.  Note that
        the length of the data, including the length of the length
        itself, is already encoded in the first 4 bytes of the
        data.

        Raises IOError if we can't write everything.

        Raises LocalError if len(data) exceeds max_payload.
        """
        size = len(data)
        assert size >= 4
        if size > self.max_payload:
            raise LocalError('data length {0} exceeds '
                             'maximum {1}'.format(size, self.max_payload))
        self.channel.sendall(data)

def _pathcat(prefix, suffix):
    """
    Concatenate paths we are using on the server side.  This is
    basically just prefix + / + suffix, with two complications:

    It's possible we don't have a prefix path, in which case
    we want the suffix without a leading slash.

    It's possible that the prefix is just b'/', in which case we
    want prefix + suffix.
    """
    if prefix:
        if prefix == b'/':  # or prefix.endswith(b'/')?
            return prefix + suffix
        return prefix + b'/' + suffix
    return suffix

class P9Client(P9SockIO):
    """
    Act as client.

    We need the a logger (see logging), a timeout, and a protocol
    version to request.  By default, we will downgrade to a lower
    version if asked.

    If server and port are supplied, they are remembered and become
    the default for .connect() (which is still deferred).

    Note that we keep a table of fid-to-path in self.live_fids,
    but at any time (except while holding the lock) a fid can
    be deleted entirely, and the table entry may just be True
    if we have no path name.  In general, we update the name
    when we can.
    """
    def __init__(self, logger, timeout, version, may_downgrade=True,
                 server=None, port=None):
        super(P9Client, self).__init__(logger)
        self.timeout = timeout
        self.iproto = protocol.p9_version(version)
        self.may_downgrade = may_downgrade
        self.tagalloc = numalloc.NumAlloc(0, 65535)
        self.tagstate = {}
        # The next bit is slighlty dirty: perhaps we should just
        # allocate NOFID out of the 2**32-1 range, so as to avoid
        # "knowing" that it's 2**32-1.
        self.fidalloc = numalloc.NumAlloc(0, protocol.td.NOFID - 1)
        self.live_fids = {}
        self.rootfid = None
        self.rootqid = None
        self.rthread = None
        self.lock = threading.Lock()
        self.new_replies = threading.Condition(self.lock)
        self._monkeywrench = {}
        self._server = server
        self._port = port
        self._unsup = {}

    def get_monkey(self, what):
        "check for a monkey-wrench"
        with self.lock:
            wrench = self._monkeywrench.get(what)
            if wrench is None:
                return None
            if isinstance(wrench, list):
                # repeats wrench[0] times, or forever if that's 0
                ret = wrench[1]
                if wrench[0] > 0:
                    wrench[0] -= 1
                    if wrench[0] == 0:
                        del self._monkeywrench[what]
            else:
                ret = wrench
                del self._monkeywrench[what]
        return ret

    def set_monkey(self, what, how, repeat=None):
        """
        Set a monkey-wrench.  If repeat is not None it is the number of
        times the wrench is applied (0 means forever, or until you call
        set again with how=None).  What is what to monkey-wrench, which
        depends on the op.  How is generally a replacement value.
        """
        if how is None:
            with self.lock:
                try:
                    del self._monkeywrench[what]
                except KeyError:
                    pass
            return
        if repeat is not None:
            how = [repeat, how]
        with self.lock:
            self._monkeywrench[what] = how

    def get_tag(self):
        "get next available tag ID"
        with self.lock:
            tag = self.tagalloc.alloc()
            if tag is None:
                raise LocalError('all tags in use')
            self.tagstate[tag] = True # ie, in use, still waiting
        return tag

    def set_tag(self, tag, reply):
        "set the reply info for the given tag"
        assert tag >= 0 and tag < 65535
        with self.lock:
            # check whether we're still waiting for the tag
            state = self.tagstate.get(tag)
            if state is True:
                self.tagstate[tag] = reply # i.e., here's the answer
                self.new_replies.notify_all()
                return
            # state must be one of these...
            if state is False:
                # We gave up on this tag.  Reply came anyway.
                self.logger.log(logging.INFO,
                                '%s: got tag %d = %r after timing out on it',
                                self, tag, reply)
                self.retire_tag_locked(tag)
                return
            if state is None:
                # We got a tag back from the server that was not
                # outstanding!
                self.logger.log(logging.WARNING,
                                '%s: got tag %d = %r when tag %d not in use!',
                                self, tag, reply, tag)
                return
            # We got a second reply before handling the first reply!
            self.logger.log(logging.WARNING,
                            '%s: got tag %d = %r when tag %d = %r!',
                            self, tag, reply, tag, state)
            return

    def retire_tag(self, tag):
        "retire the given tag - only used by the thread that handled the result"
        assert tag >= 0 and tag < 65535
        with self.lock:
            self.retire_tag_locked(tag)

    def retire_tag_locked(self, tag):
        "retire the given tag while holding self.lock"
        # must check "in tagstate" because we can race
        # with retire_all_tags.
        if tag in self.tagstate:
            del self.tagstate[tag]
            self.tagalloc.free(tag)

    def retire_all_tags(self):
        "retire all tags, after connection drop"
        with self.lock:
            # release all tags in any state (waiting, answered, timedout)
            self.tagalloc.free_multi(self.tagstate.keys())
            self.tagstate = {}
            self.new_replies.notify_all()

    def alloc_fid(self):
        "allocate new fid"
        with self.lock:
            fid = self.fidalloc.alloc()
            self.live_fids[fid] = True
        return fid

    def getpath(self, fid):
        "get path from fid, or return None if no path known, or not valid"
        with self.lock:
            path = self.live_fids.get(fid)
        if path is True:
            path = None
        return path

    def getpathX(self, fid):
        """
        Much like getpath, but return <fid N, unknown path> if necessary.
        If we do have a path, return its repr().
        """
        path = self.getpath(fid)
        if path is None:
            return '<fid {0}, unknown path>'.format(fid)
        return repr(path)

    def setpath(self, fid, path):
        "associate fid with new path (possibly from another fid)"
        with self.lock:
            if isinstance(path, int):
                path = self.live_fids.get(path)
            # path might now be None (not a live fid after all), or
            # True (we have no path name), or potentially even the
            # empty string (invalid for our purposes).  Treat all of
            # those as True, meaning "no known path".
            if not path:
                path = True
            if self.live_fids.get(fid):
                # Existing fid maps to either True or its old path.
                # Set the new path (which may be just a placeholder).
                self.live_fids[fid] = path

    def retire_fid(self, fid):
        "retire one fid"
        with self.lock:
            self.fidalloc.free(fid)
            del self.live_fids[fid]

    def retire_all_fids(self):
        "return live fids to pool"
        with self.lock:
            self.fidalloc.free_multi(self.live_fids.keys())
            self.live_fids = {}

    def read_responses(self):
        "Read responses.  This gets spun off as a thread."
        while self.is_connected():
            pkt, is_full = super(P9Client, self).read()
            if pkt == b'':
                self.shutwrite()
                self.retire_all_tags()
                return
            if not is_full:
                self.logger.log(logging.WARNING, '%s: got short packet', self)
            try:
                # We have one special case: if we're not yet connected
                # with a version, we must unpack *as if* it's a plain
                # 9P2000 response.
                if self.have_version:
                    resp = self.proto.unpack(pkt)
                else:
                    resp = protocol.plain.unpack(pkt)
            except protocol.SequenceError as err:
                self.logger.log(logging.ERROR, '%s: bad response: %s',
                                self, err)
                try:
                    resp = self.proto.unpack(pkt, noerror=True)
                except protocol.SequenceError:
                    header = self.proto.unpack_header(pkt, noerror=True)
                    self.logger.log(logging.ERROR,
                                    '%s: (not even raw-decodable)', self)
                    self.logger.log(logging.ERROR,
                                    '%s: header decode produced %r',
                                    self, header)
                else:
                    self.logger.log(logging.ERROR,
                                    '%s: raw decode produced %r',
                                    self, resp)
                # after this kind of problem, probably need to
                # shut down, but let's leave that out for a bit
            else:
                # NB: all protocol responses have a "tag",
                # so resp['tag'] always exists.
                self.logger.log(logging.DEBUG, "read_resp: tag %d resp %r", resp.tag, resp)
                self.set_tag(resp.tag, resp)

    def wait_for(self, tag):
        """
        Wait for a response to the given tag.  Return the response,
        releasing the tag.  If self.timeout is not None, wait at most
        that long (and release the tag even if there's no reply), else
        wait forever.

        If this returns None, either the tag was bad initially, or
        a timeout occurred, or the connection got shut down.
        """
        self.logger.log(logging.DEBUG, "wait_for: tag %d", tag)
        if self.timeout is None:
            deadline = None
        else:
            deadline = time.time() + self.timeout
        with self.lock:
            while True:
                # tagstate is True (waiting) or False (timedout) or
                # a valid response, or None if we've reset the tag
                # states (retire_all_tags, after connection drop).
                resp = self.tagstate.get(tag, None)
                if resp is None:
                    # out of sync, exit loop
                    break
                if resp is True:
                    # still waiting for a response - wait some more
                    self.new_replies.wait(self.timeout)
                    if deadline and time.time() > deadline:
                        # Halt the waiting, but go around once more.
                        # Note we may have killed the tag by now though.
                        if tag in self.tagstate:
                            self.tagstate[tag] = False
                    continue
                # resp is either False (timeout) or a reply.
                # If resp is False, change it to None; the tag
                # is now dead until we get a reply (then we
                # just toss the reply).
                # Otherwise, we're done with the tag: free it.
                # In either case, stop now.
                if resp is False:
                    resp = None
                else:
                    self.tagalloc.free(tag)
                    del self.tagstate[tag]
                break
        return resp

    def badresp(self, req, resp):
        """
        Complain that a response was not something expected.
        """
        if resp is None:
            self.shutdown()
            raise TEError('{0}: {1}: timeout or EOF'.format(self, req))
        if isinstance(resp, protocol.rrd.Rlerror):
            raise RemoteError(self, req, None, 'Rlerror', resp.ecode)
        if isinstance(resp, protocol.rrd.Rerror):
            if resp.errnum is None:
                raise RemoteError(self, req, resp.errstr, 'Rerror', None)
            raise RemoteError(self, req, None, 'Rerror', resp.errnum)
        raise LocalError('{0}: {1} got response {2!r}'.format(self, req, resp))

    def supports(self, req_code):
        """
        Test self.proto.support(req_code) unless we've recorded that
        while the protocol supports it, the client does not.
        """
        return req_code not in self._unsup and self.proto.supports(req_code)

    def supports_all(self, *req_codes):
        "basically just all(supports(...))"
        return all(self.supports(code) for code in req_codes)

    def unsupported(self, req_code):
        """
        Record an ENOTSUP (RemoteError was ENOTSUP) for a request.
        Must be called from the op, this does not happen automatically.
        (It's just an optimization.)
        """
        self._unsup[req_code] = True

    def connect(self, server=None, port=None):
        """
        Connect to given server/port pair.

        The server and port are remembered.  If given as None,
        the last remembered values are used.  The initial
        remembered values are from the creation of this client
        instance.

        New values are only remembered here on a *successful*
        connect, however.
        """
        if server is None:
            server = self._server
            if server is None:
                raise LocalError('connect: no server specified and no default')
        if port is None:
            port = self._port
            if port is None:
                port = STD_P9_PORT
        self.name = None            # wipe out previous name, if any
        super(P9Client, self).connect(server, port)
        maxio = self.get_recommended_maxio()
        self.declare_connected(None, None, maxio)
        self.proto = self.iproto    # revert to initial protocol
        self.have_version = False
        self.rthread = threading.Thread(target=self.read_responses)
        self.rthread.start()
        tag = self.get_tag()
        req = protocol.rrd.Tversion(tag=tag, msize=maxio,
                                    version=self.get_monkey('version'))
        super(P9Client, self).write(self.proto.pack_from(req))
        resp = self.wait_for(tag)
        if not isinstance(resp, protocol.rrd.Rversion):
            self.shutdown()
            if isinstance(resp, protocol.rrd.Rerror):
                version = req.version or self.proto.get_version()
                # for python3, we need to convert version to string
                if not isinstance(version, str):
                    version = version.decode('utf-8', 'surrogateescape')
                raise RemoteError(self, 'version ' + version,
                                  resp.errstr, 'Rerror', None)
            self.badresp('version', resp)
        their_maxio = resp.msize
        try:
            self.reduce_maxio(their_maxio)
        except LocalError as err:
            raise LocalError('{0}: sent maxio={1}, they tried {2}: '
                             '{3}'.format(self, maxio, their_maxio,
                                          err.args[0]))
        if resp.version != self.proto.get_version():
            if not self.may_downgrade:
                self.shutdown()
                raise LocalError('{0}: they only support '
                                 'version {1!r}'.format(self, resp.version))
            # raises LocalError if the version is bad
            # (should we wrap it with a connect-to-{0} msg?)
            self.proto = self.proto.downgrade_to(resp.version)
        self._server = server
        self._port = port
        self.have_version = True

    def attach(self, afid, uname, aname, n_uname):
        """
        Attach.

        Currently we don't know how to do authentication,
        but we'll pass any provided afid through.
        """
        if afid is None:
            afid = protocol.td.NOFID
        if uname is None:
            uname = ''
        if aname is None:
            aname = ''
        if n_uname is None:
            n_uname = protocol.td.NONUNAME
        tag = self.get_tag()
        fid = self.alloc_fid()
        pkt = self.proto.Tattach(tag=tag, fid=fid, afid=afid,
                                 uname=uname, aname=aname,
                                 n_uname=n_uname)
        super(P9Client, self).write(pkt)
        resp = self.wait_for(tag)
        if not isinstance(resp, protocol.rrd.Rattach):
            self.badresp('attach', resp)
        # probably should check resp.qid
        self.rootfid = fid
        self.rootqid = resp.qid
        self.setpath(fid, b'/')

    def shutdown(self):
        "disconnect from server"
        self.retire_all_tags()
        self.retire_all_fids()
        self.rootfid = None
        self.rootqid = None
        super(P9Client, self).shutdown()
        if self.rthread:
            self.rthread.join()
            self.rthread = None

    def dupfid(self, fid):
        """
        Copy existing fid to a new fid.
        """
        tag = self.get_tag()
        newfid = self.alloc_fid()
        pkt = self.proto.Twalk(tag=tag, fid=fid, newfid=newfid, nwname=0,
                               wname=[])
        super(P9Client, self).write(pkt)
        resp = self.wait_for(tag)
        if not isinstance(resp, protocol.rrd.Rwalk):
            self.badresp('walk {0}'.format(self.getpathX(fid)), resp)
        # Copy path too
        self.setpath(newfid, fid)
        return newfid

    def lookup(self, fid, components):
        """
        Do Twalk.  Caller must provide a starting fid, which should
        be rootfid to look up from '/' - we do not do / vs . here.
        Caller must also provide a component-ized path (on purpose,
        so that caller can provide invalid components like '' or '/').
        The components must be byte-strings as well, for the same
        reason.

        We do allocate the new fid ourselves here, though.

        There's no logic here to split up long walks (yet?).
        """
        # these are too easy to screw up, so check
        if self.rootfid is None:
            raise LocalError('{0}: not attached'.format(self))
        if (isinstance(components, (str, bytes) or
            not all(isinstance(i, bytes) for i in components))):
            raise LocalError('{0}: lookup: invalid '
                             'components {1!r}'.format(self, components))
        tag = self.get_tag()
        newfid = self.alloc_fid()
        startpath = self.getpath(fid)
        pkt = self.proto.Twalk(tag=tag, fid=fid, newfid=newfid,
                               nwname=len(components), wname=components)
        super(P9Client, self).write(pkt)
        resp = self.wait_for(tag)
        if not isinstance(resp, protocol.rrd.Rwalk):
            self.badresp('walk {0} in '
                         '{1}'.format(components, self.getpathX(fid)),
                         resp)
        self.setpath(newfid, _pathcat(startpath, b'/'.join(components)))
        return newfid, resp.wqid

    def lookup_last(self, fid, components):
        """
        Like lookup, but return only the last component's qid.
        As a special case, if components is an empty list, we
        handle that.
        """
        rfid, wqid = self.lookup(fid, components)
        if len(wqid):
            return rfid, wqid[-1]
        if fid == self.rootfid:         # usually true, if we get here at all
            return rfid, self.rootqid
        tag = self.get_tag()
        pkt = self.proto.Tstat(tag=tag, fid=rfid)
        super(P9Client, self).write(pkt)
        resp = self.wait_for(tag)
        if not isinstance(resp, protocol.rrd.Rstat):
            self.badresp('stat {0}'.format(self.getpathX(fid)), resp)
        return rfid, stat.qid

    def clunk(self, fid, ignore_error=False):
        "issue clunk(fid)"
        tag = self.get_tag()
        pkt = self.proto.Tclunk(tag=tag, fid=fid)
        super(P9Client, self).write(pkt)
        resp = self.wait_for(tag)
        if not isinstance(resp, protocol.rrd.Rclunk):
            if ignore_error:
                return
            self.badresp('clunk {0}'.format(self.getpathX(fid)), resp)
        self.retire_fid(fid)

    def remove(self, fid, ignore_error=False):
        "issue remove (old style), which also clunks fid"
        tag = self.get_tag()
        pkt = self.proto.Tremove(tag=tag, fid=fid)
        super(P9Client, self).write(pkt)
        resp = self.wait_for(tag)
        if not isinstance(resp, protocol.rrd.Rremove):
            if ignore_error:
                # remove failed: still need to clunk the fid
                self.clunk(fid, True)
                return
            self.badresp('remove {0}'.format(self.getpathX(fid)), resp)
        self.retire_fid(fid)

    def create(self, fid, name, perm, mode, filetype=None, extension=b''):
        """
        Issue create op (note that this may be mkdir, symlink, etc).
        fid is the directory in which the create happens, and for
        regular files, it becomes, on success, a fid referring to
        the now-open file.  perm is, e.g., 0644, 0755, etc.,
        optionally with additional high bits.  mode is a mode
        byte (e.g., protocol.td.ORDWR, or OWRONLY|OTRUNC, etc.).

        As a service to callers, we take two optional arguments
        specifying the file type ('dir', 'symlink', 'device',
        'fifo', or 'socket') and additional info if needed.
        The additional info for a symlink is the target of the
        link (a byte string), and the additional info for a device
        is a byte string with "b <major> <minor>" or "c <major> <minor>".

        Otherwise, callers can leave filetype=None and encode the bits
        into the mode (caller must still provide extension if needed).

        We do NOT check whether the extension matches extra DM bits,
        or that there's only one DM bit set, or whatever, since this
        is a testing setup.
        """
        tag = self.get_tag()
        if filetype is not None:
            perm |= {
                'dir': protocol.td.DMDIR,
                'symlink': protocol.td.DMSYMLINK,
                'device': protocol.td.DMDEVICE,
                'fifo': protocol.td.DMNAMEDPIPE,
                'socket': protocol.td.DMSOCKET,
            }[filetype]
        pkt = self.proto.Tcreate(tag=tag, fid=fid, name=name,
            perm=perm, mode=mode, extension=extension)
        super(P9Client, self).write(pkt)
        resp = self.wait_for(tag)
        if not isinstance(resp, protocol.rrd.Rcreate):
            self.badresp('create {0} in {1}'.format(name, self.getpathX(fid)),
                         resp)
        if resp.qid.type == protocol.td.QTFILE:
            # Creating a regular file opens the file,
            # thus changing the fid's path.
            self.setpath(fid, _pathcat(self.getpath(fid), name))
        return resp.qid, resp.iounit

    def open(self, fid, mode):
        "use Topen to open file or directory fid (mode is 1 byte)"
        tag = self.get_tag()
        pkt = self.proto.Topen(tag=tag, fid=fid, mode=mode)
        super(P9Client, self).write(pkt)
        resp = self.wait_for(tag)
        if not isinstance(resp, protocol.rrd.Ropen):
            self.badresp('open {0}'.format(self.getpathX(fid)), resp)
        return resp.qid, resp.iounit

    def lopen(self, fid, flags):
        "use Tlopen to open file or directory fid (flags from L_O_*)"
        tag = self.get_tag()
        pkt = self.proto.Tlopen(tag=tag, fid=fid, flags=flags)
        super(P9Client, self).write(pkt)
        resp = self.wait_for(tag)
        if not isinstance(resp, protocol.rrd.Rlopen):
            self.badresp('lopen {0}'.format(self.getpathX(fid)), resp)
        return resp.qid, resp.iounit

    def read(self, fid, offset, count):
        "read (up to) count bytes from offset, given open fid"
        tag = self.get_tag()
        pkt = self.proto.Tread(tag=tag, fid=fid, offset=offset, count=count)
        super(P9Client, self).write(pkt)
        resp = self.wait_for(tag)
        if not isinstance(resp, protocol.rrd.Rread):
            self.badresp('read {0} bytes at offset {1} in '
                         '{2}'.format(count, offset, self.getpathX(fid)),
                         resp)
        return resp.data

    def write(self, fid, offset, data):
        "write (up to) count bytes to offset, given open fid"
        tag = self.get_tag()
        pkt = self.proto.Twrite(tag=tag, fid=fid, offset=offset,
                                count=len(data), data=data)
        super(P9Client, self).write(pkt)
        resp = self.wait_for(tag)
        if not isinstance(resp, protocol.rrd.Rwrite):
            self.badresp('write {0} bytes at offset {1} in '
                         '{2}'.format(count, offset, self.getpathX(fid)),
                         resp)
        return resp.count

    def readdir(self, fid, offset, count):
        "read (up to) count bytes of dir data from offset, given open fid"
        tag = self.get_tag()
        pkt = self.proto.Treaddir(tag=tag, fid=fid, offset=offset, count=count)
        super(P9Client, self).write(pkt)
        resp = self.wait_for(tag)
        if not isinstance(resp, protocol.rrd.Rreaddir):
            self.badresp('readdir {0} bytes at offset {1} in '
                         '{2}'.format(count, offset, self.getpathX(fid)),
                         resp)
        return resp.data

    def unlinkat(self, dirfd, name, flags):
        "invoke Tunlinkat - flags should be 0 or protocol.td.AT_REMOVEDIR"
        tag = self.get_tag()
        pkt = self.proto.Tunlinkat(tag=tag, dirfd=dirfd,
                                   name=name, flags=flags)
        super(P9Client, self).write(pkt)
        resp = self.wait_for(tag)
        if not isinstance(resp, protocol.rrd.Runlinkat):
            self.badresp('unlinkat {0} in '
                         '{1}'.format(name, self.getpathX(fid)), resp)

    def decode_stat_objects(self, bstring, noerror=False):
        """
        Read on a directory returns an array of stat objects.
        Note that for .u these encode extra data.

        It's possible for this to produce a SequenceError, if
        the data are incorrect, unless you pass noerror=True.
        """
        objlist = []
        offset = 0
        while offset < len(bstring):
            obj, offset = self.proto.unpack_dirstat(bstring, offset, noerror)
            objlist.append(obj)
        return objlist

    def decode_readdir_dirents(self, bstring, noerror=False):
        """
        Readdir on a directory returns an array of dirent objects.

        It's possible for this to produce a SequenceError, if
        the data are incorrect, unless you pass noerror=True.
        """
        objlist = []
        offset = 0
        while offset < len(bstring):
            obj, offset = self.proto.unpack_dirent(bstring, offset, noerror)
            objlist.append(obj)
        return objlist

    def mkdir(self, dfid, name, mode, gid):
        "issue mkdir (.L)"
        tag = self.get_tag()
        pkt = self.proto.Tmkdir(tag=tag, dfid=dfid, name=name,
                                mode=mode, gid=gid)
        super(P9Client, self).write(pkt)
        resp = self.wait_for(tag)
        if not isinstance(resp, protocol.rrd.Rmkdir):
            self.badresp('mkdir {0} in '
                         '{1}'.format(name, self.getpathX(dfid)), resp)
        return resp.qid

    def _pathsplit(self, path, startdir, allow_empty=False):
        "common code for uxlookup and uxopen"
        if self.rootfid is None:
            raise LocalError('{0}: not attached'.format(self))
        if path.startswith(b'/') or startdir is None:
            startdir = self.rootfid
        components = [i for i in path.split(b'/') if i != b'']
        if len(components) == 0 and not allow_empty:
            raise LocalError('{0}: {1!r}: empty path'.format(self, path))
        return components, startdir

    def uxlookup(self, path, startdir=None):
        """
        Unix-style lookup.  That is, lookup('/foo/bar') or
        lookup('foo/bar').  If startdir is not None and the
        path does not start with '/' we look up from there.
        """
        components, startdir = self._pathsplit(path, startdir, allow_empty=True)
        return self.lookup_last(startdir, components)

    def uxopen(self, path, oflags=0, perm=None, startdir=None, filetype=None):
        """
        Unix-style open()-with-option-to-create, or mkdir().
        oflags is 0/1/2 with optional os.O_CREAT, perm defaults
        to 0o666 (files) or 0o777 (directories).

        Adds a final boolean value for "did we actually create".
        Raises OSError if you ask for a directory but it's a file,
        or vice versa.  (??? reconsider this later)

        Note that this does not handle other file types, only
        directories.
        """
        needtype = {
            'dir': protocol.td.QTDIR,
            None: protocol.td.QTFILE,
        }[filetype]
        omode_byte = oflags & 3 # cheating
        # allow looking up /, but not creating /
        allow_empty = (oflags & os.O_CREAT) == 0
        components, startdir = self._pathsplit(path, startdir,
                                               allow_empty=allow_empty)
        if not (oflags & os.O_CREAT):
            # Not creating, i.e., just look up and open existing file/dir.
            fid, qid = self.lookup_last(startdir, components)
            # If we got this far, use Topen on the fid; we did not
            # create the file.
            return self._uxopen2(path, needtype, fid, qid, omode_byte, False)

        if len(components) > 1:
            # Look up all but last component; this part must succeed.
            fid, _ = self.lookup(startdir, components[:-1])

            # Now proceed with the final component, using fid
            # as the start dir.
            startdir = fid
            components = components[-1:]
        else:
            # Use startdir as the start dir, and get a new fid.
            fid = self.alloc_fid()

        # Now look up the (single) component.  If this fails,
        # assume the file or directory needs to be created.
        tag = self.get_tag()
        pkt = self.proto.Twalk(tag=tag, fid=startdir, newfid=fid,
                               nwname=1, wname=components)
        super(P9Client, self).write(pkt)
        resp = self.wait_for(tag)
        if isinstance(resp, protocol.rrd.Rwalk):
            # fid successfully walked to refer to final component.
            # Just need to actually open the file.
            self.setpath(fid, _pathcat(self.getpath(startdir), components[0]))
            qid = resp.wqid[0]
            return self._uxopen2(needtype, fid, qid, omode_byte, False)

        # Try to create or mkdir as appropriate.
        if perm is None:
            perm = protocol.td.DMDIR | 0o777 if filetype == 'dir' else 0o666
        qid, iounit = self.create(fid, components[0], perm, omode_byte)

        # Success.  If we created an ordinary file, we have everything
        # now as create alters the incoming (dir) fid to open the file.
        # Otherwise (mkdir), we need to open the file, as with
        # a successful lookup.
        #
        # Note that qid type should match "needtype".
        if filetype != 'dir':
            if qid.type == needtype:
                self.setpath(fid,
                             _pathcat(self.getpath(startdir), components[0]))
                return fid, qid, iounit, True
            self.clunk(fid, ignore_error=True)
            raise OSError(_wrong_file_type(qid),
                         '{0}: server told to create {1} but '
                         'created {2} instead'.format(path,
                                                      qt2n(needtype),
                                                      qt2n(qid.type)))

        # Success: created dir; but now need to walk to and open it.
        tag = self.get_tag()
        pkt = self.proto.Twalk(tag=tag, fid=fid, newfid=fid,
                               nwname=1, wname=components)
        super(P9Client, self).write(pkt)
        resp = self.wait_for(tag)
        if not isinstance(resp, protocol.rrd.Rwalk):
            self.clunk(fid, ignore_error=True)
            raise OSError('{0}: server made dir but then failed to '
                          'find it again'.format(path))
            self.setpath(fid, _pathcat(self.getpath(fid), components[0]))
        return self._uxopen2(needtype, fid, qid, omode_byte, True)

    def _uxopen2(self, needtype, fid, qid, omode_byte, didcreate):
        "common code for finishing up uxopen"
        if qid.type != needtype:
            self.clunk(fid, ignore_error=True)
            raise OSError(_wrong_file_type(qid),
                          '{0}: is {1}, expected '
                          '{2}'.format(path, qt2n(qid.type), qt2n(needtype)))
        qid, iounit = self.open(fid, omode_byte)
        # ? should we re-check qid? it should not have changed
        return fid, qid, iounit, didcreate

    def uxmkdir(self, path, perm, gid, startdir=None):
        """
        Unix-style mkdir.

        The gid is only applied if we are using .L style mkdir.
        """
        components, startdir = self._pathsplit(path, startdir)
        clunkme = None
        if len(components) > 1:
            fid, _ = self.lookup(startdir, components[:-1])
            startdir = fid
            clunkme = fid
            components = components[-1:]
        try:
            if self.supports(protocol.td.Tmkdir):
                qid = self.mkdir(startdir, components[0], perm, gid)
            else:
                qid, _ = self.create(startdir, components[0],
                                     protocol.td.DMDIR | perm,
                                     protocol.td.OREAD)
                # Should we chown/chgrp the dir?
        finally:
            if clunkme:
                self.clunk(clunkme, ignore_error=True)
        return qid

    def uxreaddir(self, path, startdir=None, no_dotl=False):
        """
        Read a directory to get a list of names (which may or may not
        include '.' and '..').

        If no_dotl is True (or anything non-false-y), this uses the
        plain or .u readdir format, otherwise it uses dot-L readdir
        if possible.
        """
        components, startdir = self._pathsplit(path, startdir, allow_empty=True)
        fid, qid = self.lookup_last(startdir, components)
        if qid.type != protocol.td.QTDIR:
            raise OSError(errno.ENOTDIR,
                          '{0}: {1}'.format(self.getpathX(fid),
                                            os.strerror(errno.ENOTDIR)))
        # We need both Tlopen and Treaddir to use Treaddir.
        if not self.supports_all(protocol.td.Tlopen, protocol.td.Treaddir):
            no_dotl = True
        if no_dotl:
            statvals = self.uxreaddir_stat_fid(fid)
            return [stat.name for stat in statvals]

        dirents = self.uxreaddir_dotl_fid(fid)
        return [dirent.name for dirent in dirents]

    def uxreaddir_stat(self, path, startdir=None):
        """
        Use directory read to get plan9 style stat data (plain or .u readdir).

        Note that this gets a fid, then opens it, reads, then clunks
        the fid.  If you already have a fid, you may want to use
        uxreaddir_stat_fid (and you may need to dupfid it).

        We return the qid plus the list of the contents.  If the
        target is not a directory, the qid will not have type QTDIR
        and the contents list will be empty.

        Raises OSError if this is applied to a non-directory.
        """
        components, startdir = self._pathsplit(path, startdir)
        fid, qid = self.lookup_last(startdir, components)
        if qid.type != protocol.td.QTDIR:
            raise OSError(errno.ENOTDIR,
                          '{0}: {1}'.format(self.getpathX(fid),
                                            os.strerror(errno.ENOTDIR)))
        statvals = self.ux_readdir_stat_fid(fid)
        return qid, statvals

    def uxreaddir_stat_fid(self, fid):
        """
        Implement readdir loop that extracts stat values.
        Clunks the fid when done (since we always open it).
        Clunks the fid even if it's not a directory or open fails.

        Unlike uxreaddir_stat(), if this is applied to a file,
        rather than a directory, it just returns no entries.
        """
        statvals = []
        try:
            qid, iounit = self.open(fid, protocol.td.OREAD)
            # ?? is a zero iounit allowed? if so, what do we use here?
            if qid.type == protocol.td.QTDIR:
                if iounit <= 0:
                    iounit = 512 # probably good enough
                offset = 0
                while True:
                    bstring = self.read(fid, offset, iounit)
                    if bstring == b'':
                        break
                    statvals.extend(self.decode_stat_objects(bstring))
                    offset += len(bstring)
        finally:
            self.clunk(fid, ignore_error=True)
        return statvals

    def uxreaddir_dotl_fid(self, fid):
        """
        Implement readdir loop that uses dot-L style dirents.
        Clunks the fid when done (since we always open it).
        Clunks the fid even if it's not a directory or lopen fails.

        If applied to a file, the lopen should fail, because of the
        L_O_DIRECTORY flag.
        """
        dirents = []
        try:
            qid, iounit = self.lopen(fid, protocol.td.OREAD |
                                          protocol.td.L_O_DIRECTORY)
            # ?? is a zero iounit allowed? if so, what do we use here?
            # but, we want a minimum of over 256 anyway, let's go for 512
            if iounit < 512:
                iounit = 512
            offset = 0
            while True:
                bstring = self.readdir(fid, offset, iounit)
                if bstring == b'':
                    break
                dirents.extend(self.decode_readdir_dirents(bstring))
                offset += len(bstring)
        finally:
            self.clunk(fid, ignore_error=True)
        return dirents

    def uxremove(self, path, startdir=None, filetype=None,
                 force=False, recurse=False):
        """
        Implement rm / rmdir, with optional -rf.
        if filetype is None, remove dir or file.  If 'dir' or 'file'
        remove only if it's one of those.  If force is set, ignore
        failures to remove.  If recurse is True, remove contents of
        directories (recursively).

        File type mismatches (when filetype!=None) raise OSError (?).
        """
        components, startdir = self._pathsplit(path, startdir, allow_empty=True)
        # Look up all components. If
        # we get an error we'll just assume the file does not
        # exist (is this good?).
        try:
            fid, qid = self.lookup_last(startdir, components)
        except RemoteError:
            return
        if qid.type == protocol.td.QTDIR:
            # it's a directory, remove only if allowed.
            # Note that we must check for "rm -r /" (len(compoents)==0).
            if filetype == 'file':
                raise OSError(_wrong_file_type(qid),
                              '{0}: is dir, expected file'.format(path))
            finalfunc = self.remove if len(components) else self.clunk
            if recurse:
                try:
                    self._rm_recursive(fid, filetype, force)
                finally:
                    finalfunc(fid, ignore_error=force)
                return
            # This will fail if the directory is non-empty, unless of
            # course we tell it to ignore error.
            finalfunc(fid, ignore_error=force)
            return
        # Not a directory, call it a file (even if socket or fifo etc).
        if filetype == 'dir':
            raise OSError(_wrong_file_type(qid),
                          '{0}: is file, expected dir'.format(path))
        self.remove(fid, ignore_error=force)

    def _rm_file_by_dfid(self, dfid, name, force=False):
        """
        Remove a file whose name is <name> (no path, just a component
        name) whose parent directory is <dfid>.  We may assume that the
        file really is a file (or a socket, or fifo, or some such, but
        definitely not a directory).

        If force is set, ignore failures.
        """
        # If we have unlinkat, that's the fast way.  But it may
        # return an ENOTSUP error.  If it does we shouldn't bother
        # doing this again.
        if self.supports(protocol.td.Tunlinkat):
            try:
                self.unlinkat(dfid, name, 0)
                return
            except RemoteError as err:
                if not err.is_ENOTSUP():
                    raise
                self.unsupported(protocol.td.Tunlinkat)
                # fall through to remove() op
        # Fall back to lookup + remove.
        try:
            fid, qid = self.lookup_last(dfid, [name])
        except RemoteError:
            # If this has an errno we could tell ENOENT from EPERM,
            # and actually raise an error for the latter.  Should we?
            return
        self.remove(fid, ignore_error=force)

    def _rm_recursive(self, dfid, filetype, force):
        """
        Recursively remove a directory.  filetype is probably None,
        but if it's 'dir' we fail if the directory contains non-dir
        files.

        If force is set, ignore failures.
        """
        # first, remove contents
        for stat in self.uxreaddir_stat_fid(self.dupfid(dfid)):
            # skip . and ..
            name = stat.name
            if name in (b'.', b'..'):
                continue
            if stat.qid.type == protocol.td.QTDIR:
                self.uxremove(name, dfid, filetype, force, True)
            else:
                self._rm_file_by_dfid(dfid, name, force)

def _wrong_file_type(qid):
    "return EISDIR or ENOTDIR for passing to OSError"
    if qid.type == protocol.td.QTDIR:
        return errno.EISDIR
    return errno.ENOTDIR
