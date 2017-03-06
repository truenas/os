/*-
 * Copyright (c) 2002-2010 M. Warner Losh.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * my_system is a variation on lib/libc/stdlib/system.c:
 *
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * DEVD control daemon.
 */

// TODO list:
//	o devd.conf and devd man pages need a lot of help:
//	  - devd needs to document the unix domain socket
//	  - devd.conf needs more details on the supported statements.

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/un.h>

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <csignal>
#include <cstring>
#include <cstdarg>

#include <dirent.h>
#include <err.h>
#include <fcntl.h>
#include <libutil.h>
#include <paths.h>
#include <poll.h>
#include <regex.h>
#include <syslog.h>
#include <unistd.h>
#include <bsdxml.h>

#include <algorithm>
#include <map>
#include <string>
#include <list>
#include <stdexcept>
#include <vector>

#include "bwrite.h"		/* buffered writer code */
#include "devd.h"		/* C compatible definitions */
#include "devd.hh"		/* C++ class definitions */

#define STREAMPIPE_XML		"/var/run/devd.xml.pipe"
#define STREAMPIPE_COMPAT	"/var/run/devd.pipe"
#define SEQPACKETPIPE_XML	"/var/run/devd.xml.seqpacket.pipe"
#define SEQPACKETPIPE_COMPAT	"/var/run/devd.seqpacket.pipe"
#define CF "/etc/devd.conf"
#define SYSCTL "hw.bus.devctl_queue"

/*
 * We used to try to do nonblocking sends to each client, with very
 * large kernel-side send buffer limits, but this does not work well
 * on all sockets.  Instead, we now use a POSIX	thread per client.
 * The overall strategy remains the same: we need enough buffer to
 * handle brief event storms.
 *
 * Experimentally, with some recent zfs changes, 256K per client
 * is not enough.  For the moment, let's make this 1MB per client.
 *
 * The bwrite code needs one record-length indicator per record, as
 * well.  We'll assume records average about 128 bytes for this
 * purpose.
 */
#define CLIENT_BUFSIZE (1 * 1024 * 1024)
#define	CLIENT_MAXRECS (CLIENT_BUFSIZE / 128)

static size_t client_bufsize = CLIENT_BUFSIZE;
static size_t client_maxrecs = CLIENT_MAXRECS;
static int client_debug = -1;

using namespace std;

typedef struct client {
	int fd;
	int socktype;
	bool is_xml;
	struct bwrite *bwritep;
	char *data;
	size_t *records;	/* if socktype==SOCK_SEQPACKET */
} client_t;

extern FILE *yyin;
extern int lineno;

static const char notify = '!';
static const char nomatch = '?';
static const char attach = '+';
static const char detach = '-';

static struct pidfh *pfh;

static int no_daemon = 0;
static int daemonize_quick = 0;
static int quiet_mode = 0;
static unsigned total_events = 0;
static volatile sig_atomic_t got_siginfo = 0;
static volatile sig_atomic_t romeo_must_die = 0;

static const char *configfile = CF;

static void devdlog(int priority, const char* message, ...)
	__printflike(2, 3);
static void event_loop(void);
static void usage(void);

template <class T> void
delete_and_clear(vector<T *> &v)
{
	typename vector<T *>::const_iterator i;

	for (i = v.begin(); i != v.end(); ++i)
		delete *i;
	v.clear();
}

config cfg;

event_proc::event_proc() : _prio(-1)
{
	_epsvec.reserve(4);
}

event_proc::~event_proc()
{
	delete_and_clear(_epsvec);
}

void
event_proc::add(eps *eps)
{
	_epsvec.push_back(eps);
}

bool
event_proc::matches(config &c) const
{
	vector<eps *>::const_iterator i;

	for (i = _epsvec.begin(); i != _epsvec.end(); ++i)
		if (!(*i)->do_match(c))
			return (false);
	return (true);
}

bool
event_proc::run(config &c) const
{
	vector<eps *>::const_iterator i;

	for (i = _epsvec.begin(); i != _epsvec.end(); ++i)
		if (!(*i)->do_action(c))
			return (false);
	return (true);
}

action::action(const char *cmd)
	: _cmd(cmd)
{
	// nothing
}

action::~action()
{
	// nothing
}

static int
my_system(const char *command)
{
	pid_t pid, savedpid;
	int pstat;
	struct sigaction ign, intact, quitact;
	sigset_t newsigblock, oldsigblock;

	if (!command)		/* just checking... */
		return (1);

	/*
	 * Ignore SIGINT and SIGQUIT, block SIGCHLD. Remember to save
	 * existing signal dispositions.
	 */
	ign.sa_handler = SIG_IGN;
	::sigemptyset(&ign.sa_mask);
	ign.sa_flags = 0;
	::sigaction(SIGINT, &ign, &intact);
	::sigaction(SIGQUIT, &ign, &quitact);
	::sigemptyset(&newsigblock);
	::sigaddset(&newsigblock, SIGCHLD);
	::sigprocmask(SIG_BLOCK, &newsigblock, &oldsigblock);
	switch (pid = ::fork()) {
	case -1:			/* error */
		break;
	case 0:				/* child */
		/*
		 * Restore original signal dispositions and exec the command.
		 */
		::sigaction(SIGINT, &intact, NULL);
		::sigaction(SIGQUIT,  &quitact, NULL);
		::sigprocmask(SIG_SETMASK, &oldsigblock, NULL);
		/*
		 * Close the PID file, and all other open descriptors.
		 * Inherit std{in,out,err} only.
		 */
		cfg.close_pidfile();
		::closefrom(3);
		::execl(_PATH_BSHELL, "sh", "-c", command, (char *)NULL);
		::_exit(127);
	default:			/* parent */
		savedpid = pid;
		do {
			pid = ::wait4(savedpid, &pstat, 0, (struct rusage *)0);
		} while (pid == -1 && errno == EINTR);
		break;
	}
	::sigaction(SIGINT, &intact, NULL);
	::sigaction(SIGQUIT,  &quitact, NULL);
	::sigprocmask(SIG_SETMASK, &oldsigblock, NULL);
	return (pid == -1 ? -1 : pstat);
}

bool
action::do_action(config &c)
{
	string s = c.expand_string(_cmd.c_str());
	devdlog(LOG_INFO, "Executing '%s'\n", s.c_str());
	my_system(s.c_str());
	return (true);
}

match::match(config &c, const char *var, const char *re) :
	_inv(re[0] == '!'),
	_var(var),
	_re(c.expand_string(_inv ? re + 1 : re, "^", "$"))
{
	regcomp(&_regex, _re.c_str(), REG_EXTENDED | REG_NOSUB | REG_ICASE);
}

match::~match()
{
	regfree(&_regex);
}

bool
match::do_match(config &c)
{
	const string &value = c.get_variable(_var);
	bool retval;

	/*
	 * This function gets called WAY too often to justify calling syslog()
	 * each time, even at LOG_DEBUG.  Because if syslogd isn't running, it
	 * can consume excessive amounts of systime inside of connect().  Only
	 * log when we're in -d mode.
	 */
	if (no_daemon && !quiet_mode) {
		devdlog(LOG_DEBUG, "Testing %s=%s against %s, invert=%d\n",
		    _var.c_str(), value.c_str(), _re.c_str(), _inv);
	}

	retval = (regexec(&_regex, value.c_str(), 0, NULL, 0) == 0);
	if (_inv == 1)
		retval = (retval == 0) ? 1 : 0;

	return (retval);
}

#include <sys/sockio.h>
#include <net/if.h>
#include <net/if_media.h>

media::media(config &, const char *var, const char *type)
	: _var(var), _type(-1)
{
	static struct ifmedia_description media_types[] = {
		{ IFM_ETHER,		"Ethernet" },
		{ IFM_TOKEN,		"Tokenring" },
		{ IFM_FDDI,		"FDDI" },
		{ IFM_IEEE80211,	"802.11" },
		{ IFM_ATM,		"ATM" },
		{ -1,			"unknown" },
		{ 0, NULL },
	};
	for (int i = 0; media_types[i].ifmt_string != NULL; ++i)
		if (strcasecmp(type, media_types[i].ifmt_string) == 0) {
			_type = media_types[i].ifmt_word;
			break;
		}
}

media::~media()
{
}

bool
media::do_match(config &c)
{
	string value;
	struct ifmediareq ifmr;
	bool retval;
	int s;

	// Since we can be called from both a device attach/detach
	// context where device-name is defined and what we want,
	// as well as from a link status context, where subsystem is
	// the name of interest, first try device-name and fall back
	// to subsystem if none exists.
	value = c.get_variable("device-name");
	if (value.empty())
		value = c.get_variable("subsystem");
	devdlog(LOG_DEBUG, "Testing media type of %s against 0x%x\n",
		    value.c_str(), _type);

	retval = false;

	s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s >= 0) {
		memset(&ifmr, 0, sizeof(ifmr));
		strlcpy(ifmr.ifm_name, value.c_str(), sizeof(ifmr.ifm_name));

		if (ioctl(s, SIOCGIFMEDIA, (caddr_t)&ifmr) >= 0 &&
		    ifmr.ifm_status & IFM_AVALID) {
			devdlog(LOG_DEBUG, "%s has media type 0x%x\n",
				    value.c_str(), IFM_TYPE(ifmr.ifm_active));
			retval = (IFM_TYPE(ifmr.ifm_active) == _type);
		} else if (_type == -1) {
			devdlog(LOG_DEBUG, "%s has unknown media type\n",
				    value.c_str());
			retval = true;
		}
		close(s);
	}

	return (retval);
}

const string var_list::bogus = "_$_$_$_$_B_O_G_U_S_$_$_$_$_";
const string var_list::nothing = "";

const string &
var_list::get_variable(const string &var) const
{
	map<string, string>::const_iterator i;

	i = _vars.find(var);
	if (i == _vars.end())
		return (var_list::bogus);
	return (i->second);
}

bool
var_list::is_set(const string &var) const
{
	return (_vars.find(var) != _vars.end());
}

void
var_list::set_variable(const string &var, const string &val)
{
	/*
	 * This function gets called WAY too often to justify calling syslog()
	 * each time, even at LOG_DEBUG.  Because if syslogd isn't running, it
	 * can consume excessive amounts of systime inside of connect().  Only
	 * log when we're in -d mode.
	 */
	if (no_daemon && !quiet_mode)
		devdlog(LOG_DEBUG, "setting %s=%s\n", var.c_str(), val.c_str());
	_vars[var] = val;
}

void
config::reset(void)
{
	_dir_list.clear();
	delete_and_clear(_var_list_table);
	delete_and_clear(_attach_list);
	delete_and_clear(_detach_list);
	delete_and_clear(_nomatch_list);
	delete_and_clear(_notify_list);
}

void
config::parse_one_file(const char *fn)
{
	devdlog(LOG_DEBUG, "Parsing %s\n", fn);
	yyin = fopen(fn, "r");
	if (yyin == NULL)
		err(1, "Cannot open config file %s", fn);
	lineno = 1;
	if (yyparse() != 0)
		errx(1, "Cannot parse %s at line %d", fn, lineno);
	fclose(yyin);
}

void
config::parse_files_in_dir(const char *dirname)
{
	DIR *dirp;
	struct dirent *dp;
	char path[PATH_MAX];

	devdlog(LOG_DEBUG, "Parsing files in %s\n", dirname);
	dirp = opendir(dirname);
	if (dirp == NULL)
		return;
	readdir(dirp);		/* Skip . */
	readdir(dirp);		/* Skip .. */
	while ((dp = readdir(dirp)) != NULL) {
		if (strcmp(dp->d_name + dp->d_namlen - 5, ".conf") == 0) {
			snprintf(path, sizeof(path), "%s/%s",
			    dirname, dp->d_name);
			parse_one_file(path);
		}
	}
	closedir(dirp);
}

class epv_greater {
public:
	int operator()(event_proc *const&l1, event_proc *const&l2) const
	{
		return (l1->get_priority() > l2->get_priority());
	}
};

void
config::sort_vector(vector<event_proc *> &v)
{
	stable_sort(v.begin(), v.end(), epv_greater());
}

void
config::parse(void)
{
	vector<string>::const_iterator i;

	parse_one_file(configfile);
	for (i = _dir_list.begin(); i != _dir_list.end(); ++i)
		parse_files_in_dir((*i).c_str());
	sort_vector(_attach_list);
	sort_vector(_detach_list);
	sort_vector(_nomatch_list);
	sort_vector(_notify_list);
}

void
config::open_pidfile()
{
	pid_t otherpid;

	if (_pidfile.empty())
		return;
	pfh = pidfile_open(_pidfile.c_str(), 0600, &otherpid);
	if (pfh == NULL) {
		if (errno == EEXIST)
			errx(1, "devd already running, pid: %d", (int)otherpid);
		warn("cannot open pid file");
	}
}

void
config::write_pidfile()
{

	pidfile_write(pfh);
}

void
config::close_pidfile()
{

	pidfile_close(pfh);
}

void
config::remove_pidfile()
{

	pidfile_remove(pfh);
}

void
config::add_attach(int prio, event_proc *p)
{
	p->set_priority(prio);
	_attach_list.push_back(p);
}

void
config::add_detach(int prio, event_proc *p)
{
	p->set_priority(prio);
	_detach_list.push_back(p);
}

void
config::add_directory(const char *dir)
{
	_dir_list.push_back(string(dir));
}

void
config::add_nomatch(int prio, event_proc *p)
{
	p->set_priority(prio);
	_nomatch_list.push_back(p);
}

void
config::add_notify(int prio, event_proc *p)
{
	p->set_priority(prio);
	_notify_list.push_back(p);
}

void
config::set_pidfile(const char *fn)
{
	_pidfile = fn;
}

void
config::push_var_table()
{
	var_list *vl;

	vl = new var_list();
	_var_list_table.push_back(vl);
	devdlog(LOG_DEBUG, "Pushing table\n");
}

void
config::pop_var_table()
{
	delete _var_list_table.back();
	_var_list_table.pop_back();
	devdlog(LOG_DEBUG, "Popping table\n");
}

void
config::set_variable(const string &var, const string &val)
{
	_var_list_table.back()->set_variable(var, val);
}

const string &
config::get_variable(const string &var)
{
	vector<var_list *>::reverse_iterator i;

	for (i = _var_list_table.rbegin(); i != _var_list_table.rend(); ++i) {
		if ((*i)->is_set(var))
			return ((*i)->get_variable(var));
	}
	return (var_list::nothing);
}

bool
config::is_id_char(char ch) const
{
	return (ch != '\0' && (isalpha(ch) || isdigit(ch) || ch == '_' ||
	    ch == '-'));
}

void
config::expand_one(const char *&src, string &dst)
{
	int count;
	string buffer;

	src++;
	// $$ -> $
	if (*src == '$') {
		dst += *src++;
		return;
	}

	// $(foo) -> $(foo)
	// Not sure if I want to support this or not, so for now we just pass
	// it through.
	if (*src == '(') {
		dst += '$';
		count = 1;
		/* If the string ends before ) is matched , return. */
		while (count > 0 && *src) {
			if (*src == ')')
				count--;
			else if (*src == '(')
				count++;
			dst += *src++;
		}
		return;
	}

	// $[^-A-Za-z_*] -> $\1
	if (!isalpha(*src) && *src != '_' && *src != '-' && *src != '*') {
		dst += '$';
		dst += *src++;
		return;
	}

	// $var -> replace with value
	do {
		buffer += *src++;
	} while (is_id_char(*src));
	dst.append(get_variable(buffer));
}

const string
config::expand_string(const char *src, const char *prepend, const char *append)
{
	const char *var_at;
	string dst;

	/*
	 * 128 bytes is enough for 2427 of 2438 expansions that happen
	 * while parsing config files, as tested on 2013-01-30.
	 */
	dst.reserve(128);

	if (prepend != NULL)
		dst = prepend;

	for (;;) {
		var_at = strchr(src, '$');
		if (var_at == NULL) {
			dst.append(src);
			break;
		}
		dst.append(src, var_at - src);
		src = var_at;
		expand_one(src, dst);
	}

	if (append != NULL)
		dst.append(append);

	return (dst);
}

bool
config::chop_var(char *&buffer, char *&lhs, char *&rhs) const
{
	char *walker;

	if (*buffer == '\0')
		return (false);
	walker = lhs = buffer;
	while (is_id_char(*walker))
		walker++;
	if (*walker != '=')
		return (false);
	walker++;		// skip =
	if (*walker == '"') {
		walker++;	// skip "
		rhs = walker;
		while (*walker && *walker != '"')
			walker++;
		if (*walker != '"')
			return (false);
		rhs[-2] = '\0';
		*walker++ = '\0';
	} else {
		rhs = walker;
		while (*walker && !isspace(*walker))
			walker++;
		if (*walker != '\0')
			*walker++ = '\0';
		rhs[-1] = '\0';
	}
	while (isspace(*walker))
		walker++;
	buffer = walker;
	return (true);
}

void
config::set_vars(const event_t &event)
{
	for (auto const &it: event.params) {
		cfg.set_variable(it.first, it.second);

		/*
		 * "location" and "pnp" params are obtained from newbus
		 * methods and are in form of space-separated key-value
		 * pair strings. We need to parse them in the way old
		 * devd did until newbus code gets switched to use
		 * structured parameters.
		 */
		if (it.first.compare("location") == 0 ||
		    it.first.compare("pnp") == 0) {
			char *buffer = strdup(it.second.c_str());
			char *tmp = buffer;
			char *lhs, *rhs;

			while (1) {
				if (!chop_var(tmp, lhs, rhs))
					break;
				cfg.set_variable(lhs, rhs);
			}

			free(buffer);
		}
	}
}

void
config::find_and_execute(const string type)
{
	vector<event_proc *> *l = NULL;
	vector<event_proc *>::const_iterator i;

	if (type == "notify")
		l = &_notify_list;
	else if (type == "nomatch")
		l = &_nomatch_list;
	else if (type == "attach")
		l = &_attach_list;
	else if (type == "detach")
		l = &_detach_list;

	if (!l)
		return;

	devdlog(LOG_DEBUG, "Processing %s event\n", type.c_str());
	for (i = l->begin(); i != l->end(); ++i) {
		if ((*i)->matches(*this)) {
			(*i)->run(*this);
			break;
		}
	}

}

static void
handle_start_element(void *priv, const char *name, const char **attrs)
{
	event_t *event = (event_t *)priv;

	if (event->type.empty()) {
		event->type = name;
		return;
	}

	event->key = name;
}

static void
handle_end_element(void *priv, const char *name)
{
	event_t *event = (event_t *)priv;

	event->key.clear();
}

static void
handle_data(void *priv, const char *data, int length)
{
	event_t *event = (event_t *)priv;

	if (!event->key.empty()) {
		event->params[event->key] = string(data, length);
		event->key.clear();
	}
}

static bool
parse_event(char *buffer, event_t &event)
{
	int ret;

	XML_Parser parser = XML_ParserCreate(NULL);
	XML_SetElementHandler(parser, handle_start_element, handle_end_element);
	XML_SetCharacterDataHandler(parser, handle_data);
	XML_SetUserData(parser, &event);
	ret = XML_Parse(parser, buffer, strlen(buffer), XML_TRUE);
	XML_ParserFree(parser);

	return (ret == XML_STATUS_ERROR);
}


static void
process_event(char *buffer)
{
	event_t event;

	if (parse_event(buffer, event))
		return;

	devdlog(LOG_INFO, "Processing event '%s'\n", buffer);
	cfg.push_var_table();
	cfg.set_vars(event);
	cfg.find_and_execute(event.type);
	cfg.pop_var_table();
}

int
create_socket(const char *name, int socktype)
{
	int fd, slen;
	struct sockaddr_un sun;

	if ((fd = socket(PF_LOCAL, socktype, 0)) < 0)
		err(1, "socket");
	bzero(&sun, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, name, sizeof(sun.sun_path));
	slen = SUN_LEN(&sun);
	unlink(name);
	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
	    	err(1, "fcntl");
	if (::bind(fd, (struct sockaddr *) & sun, slen) < 0)
		err(1, "bind");
	listen(fd, 4);
	if (chown(name, 0, 0))	/* XXX - root.wheel */
		err(1, "chown");
	if (chmod(name, 0666))
		err(1, "chmod");
	return (fd);
}

unsigned int max_clients = 10;	/* Default, can be overridden on cmdline. */
unsigned int num_clients;

list<client_t> clients;

void
ditch_client(struct client &cli, const char *caller, const char *reason,
    bool log_errno)
{
	const char *fmt;

	if (client_debug >= 0) {
		if (log_errno)
			fmt = "%s: dropping %s client fd=%d: %s\n";
		else
			fmt = "%s: dropping %s client fd=%d\n";
		devdlog(client_debug, fmt, caller, reason, cli.fd,
		    strerror(errno));
	} else {
		if (log_errno)
			fmt = "%s: dropping %s client: %s\n";
		else
			fmt = "%s: dropping %s client\n";
		devdlog(LOG_WARNING, fmt, caller, reason,
		    strerror(errno));
	}
	bw_fini(cli.bwritep);
	delete[] cli.records;
	delete[] cli.data;
	delete cli.bwritep;
	close(cli.fd);
	--num_clients;
}

void
notify_clients(const char *xml, int xmllen, const char *compat, int compatlen)
{
	list<client_t>::iterator i;

	/*
	 * Queue the data to all clients.  Throw clients overboard if
	 * queue-put fails.  This reaps clients who've died or closed
	 * their sockets, and also clients who are alive but failing to keep up
	 * (or who are maliciously not reading, to consume buffer space
	 * or tie up the limited number of available connections).
	 *
	 * (Note that when a write fails asynchronously, it causes
	 * the *next* queue-put to fail.)
	 */
	for (i = clients.begin(); i != clients.end(); ) {
		const char *data;
		int len;
		enum bw_put_result rv;

		if (i->is_xml) {
			data = xml;
			len = xmllen;
		} else {
			data = compat;
			len = compatlen;
		}
		rv = bw_put(i->bwritep, (void *)data, len);
		if (rv != BW_PUT_OK) {
			ditch_client(*i, __func__, "unresponsive",
			   rv == BW_PUT_ERROR);
			i = clients.erase(i);
		} else
			++i;
	}
}

void
check_clients(void)
{
	list<client_t>::iterator i;

	/*
	 * Check all existing clients to see if any of them have disappeared.
	 * Normally we reap clients when we get an error trying to send them an
	 * event.  This check eliminates the problem of an ever-growing list of
	 * zombie clients because we're never writing to them on a system
	 * without frequent device-change activity.
	 *
	 * (It also now catches de-queued sends that failed in a thread,
	 * which, again, we'd notice on a later put.)
	 */
	for (i = clients.begin(); i != clients.end(); ) {
		enum bw_state state;

		state = bw_check(i->bwritep, BW_CHECK_HUP);
		/* NB: blocked state is OK, just means we're queuing data */
		if (state != BW_OPEN && state != BW_BLOCKED) {
			ditch_client(*i, __func__, "disconnected", false);
			i = clients.erase(i);
		} else {
			if (client_debug >= 0) {
				size_t ddepth, dmax, rdepth, rmax;
				double dpct, rpct;

				bw_get_qdepth(i->bwritep,
				    &ddepth, &dmax, &rdepth, &rmax);
				dpct = 100.0 * ddepth / dmax;
				if (i->socktype == SOCK_SEQPACKET) {
					rpct = 100.0 * rdepth / rmax;
					devdlog(client_debug,
					    "record client on fd %d: ddepth "
					    "%zu/%zu (%.2f%%), rdepth "
					    "%zu (%.2f%%)\n",
					    i->fd,
					    ddepth, dmax, dpct,
					    rdepth, rpct);
				} else {
					devdlog(client_debug,
					    "stream client on fd %d: ddepth "
					    "%zu/%zu (%.2f%%)\n",
					    i->fd,
					    ddepth, dmax, dpct);
				}
			}
			++i;
		}
	}
}

void
new_client(int fd, bool is_xml, int socktype)
{
	client_t s;
	size_t datasize, nrec;
	int bwflags, error;

	/*
	 * First go reap any zombie clients, then accept the connection, and
	 * shut down the read side to stop clients from consuming kernel memory
	 * by sending large buffers full of data we'll never read.
	 */
	if (client_debug >= 0)
		devdlog(client_debug,
		    "new_client: fd %d, is%s xml, type %s\n",
		    fd, is_xml ? "" : " not",
		    socktype == SOCK_SEQPACKET ? "record" : "stream");
	check_clients();
	s.socktype = socktype;
	s.is_xml = is_xml;
	s.fd = accept(fd, NULL, NULL);
	if (s.fd == -1)
		err(1, "accept");

	/*
	 * These sizes should be tunable; this is where you would
	 * tune them.
	 */
	datasize = client_bufsize;
	nrec = client_maxrecs;
	s.bwritep = NULL;
	s.data = NULL;
	s.records = NULL;
	bwflags = BW_STREAMING;
	error = 0;
	try {
		s.bwritep = new bwrite;
		s.data = new char[datasize];
		if (socktype == SOCK_STREAM)
			nrec = 0;	/* no records needed */
		else {
			s.records = new size_t[nrec];
			bwflags = BW_RECORDS;
		}
		error = bw_init(s.bwritep, s.fd, s.data, datasize,
		    s.records, nrec, bwflags);
	} catch (std::bad_alloc) {
		/* one or more of the several new operators above failed */
		error = 1;
	}
	if (error) {
		devdlog(LOG_ERR, "error setting up new client: %s\n",
		    strerror(errno));
		delete[] s.records;
		delete[] s.data;
		delete[] s.bwritep;
		close(s.fd);
		return;
	}
	shutdown(s.fd, SHUT_RD);
	clients.push_back(s);
	++num_clients;
	if (client_debug >= 0)
		devdlog(client_debug, "now have %d clients\n", num_clients);
}

/*
 * The compat (i.e., original) format, which external programs
 * may still use, generally looks like:
 *     "!system=%s subsystem=%s type=%s ...\n"
 * i.e., a !-prefixed, \n-terminated set of name=value pairs.
 * The '!' is sometimes '+', '-', or '?' instead (see below).
 *
 * The new XML format is generally:
 *     "<notify><system>%s</system><subsystem>%s</subsystem>"
 * usually followed by:
 *     "<type>%s</type><data>%s</data>" etc
 * and then:
 *     "</notify>"
 * So, we simply convert each XML tag except for the first
 * to <tag>=%s, with spaces before each one.  Adding a newline and
 * replacing the first space with '!' gives us the compat format.
 *
 * For attach and detach events, however, we get:
 *     "<%s><device-name>%s</device-name>"
 * The first %s is a literal "attach", "detach", or "nomatch"
 * and the second is the device name, such as "axe0".  This is
 * immediately followed by:
 *     "<location>%s</location><pnp>%s</pnp><bus>%s</bus></%s>
 * (note that these do not begin with <notify>).
 *
 * These must convert differently.  Their compat format is:
 *     "%c%s at %s %s on %s\n"
 * The initial %c character is '+' if the first tag is <attach>,
 * '-' for <detach>, and '?' for <nomatch>.  Meanwhile the %s
 * strings are the device-name, location, pnp, and bus arguments
 * from the XML.  For simplicity here we require rigid ordering
 * of the incoming tags.
 *
 * De-XML-ing should never result in overrun of the given buffer
 * since the XML form is considerably bulkier, but I've included
 * truncation-detection code that could be turned on if desired.
 */
class flatten {
public:
	char   *fl_buf;
	int	fl_len;
	int	fl_size;
	int	fl_sequencer;
	bool	fl_seqerror;
	bool	fl_trunc;

	flatten(char *buf, int bufsize) {
		fl_buf = buf;
		fl_len = 0;
		fl_size = bufsize;
		fl_sequencer = 0;
		fl_seqerror = false;
		fl_trunc = false;
	}

	void add(const char *text, int textlen) {
		int space = fl_size - fl_len;

		if (textlen > space) {
			fl_trunc = true;
			if (space <= 0)
				return;
			textlen = space;
		}
		memcpy(fl_buf + fl_len, text, textlen);
		fl_len += textlen;
	}

	// Handle XML element name.
	void element(const char *name) {
		char prefix;

		switch (fl_sequencer) {
		case -1:
			// Notify: just pass through as " tag=..."
			// (the "..." part comes from the data).
			// We'll replace the initial " " with "!"
			// later.
			add(" ", 1);
			add(name, strlen(name));
			add("=", 1);
			return;
		case 0:
			if (strcmp(name, "attach") == 0) {
				prefix = '+';
			} else if (strcmp(name, "detach") == 0) {
				prefix = '-';
			} else if (strcmp(name, "nomatch") == 0) {
				prefix = '?';
			} else {
				// assume <notify>, pass remainder through
				fl_sequencer = -1;
				return;
			}
			add(&prefix, 1);
			break;
		case 1:	// Expecting "<device-name>%s</device-name>.
			// For compatibility with slightly old kernel
			// let's accept <name>%s</name> too.
			if (strcmp(name, "device-name") != 0 &&
			    strcmp(name, "name") != 0)
				fl_seqerror = true;
			// device name comes from fl_data
			break;

		case 2:	// Expecting <location>.
			if (strcmp(name, "location") == 0)
				add(" at ", 4);
			else
				fl_seqerror = true;
			// rest of location comes from fl_data
			break;

		case 3:	// Expecting <pnp>.
			if (strcmp(name, "pnp") == 0)
				add(" ", 1);
			else
				fl_seqerror = true;
			break;

		case 4:	// Expecting <bus>.
			// Accept <parent> as well (see device-name).
			if (strcmp(name, "bus") == 0 ||
			    strcmp(name, "parent") == 0)
				add(" on ", 4);
			else
				fl_seqerror = true;
			break;

		default:
			// should not happen
			fl_seqerror = true;
			return;
		}
		++fl_sequencer;
	}

	void finalize() {
		// replace initial ' ' with '!' for <notify> events
		if (fl_sequencer < 0)
			fl_buf[0] = '!';
		// add terminating newline
		add("\n", 1);
	}
};

static void
fl_element(void *priv, const char *name, const char **attrs)
{
	flatten *fl = (flatten *)priv;

	fl->element(name);
}

static void
fl_data(void *priv, const char *data, int length)
{
	flatten *fl = (flatten *)priv;

	// Annoying special case: the kernel now provides USB
	// <location> data as "busno=%d hubaddr=%d ..."
	// when it used to read "bus=%d hubaddr=%d ...".  To
	// maintain 100% compatibility, we must replace
	// the "busno" string with "bus".
	//
	// (It also adds ugen=ugen1.2, which was not present
	// before, but I think we can ignore that.)
	//
	// (The sequencer magic value of 3 here is ugly, see
	// flatten::element for where it came from.)
	if (fl->fl_sequencer == 3 && memcmp(data, "busno=", 6) == 0) {
		fl->add("bus", 3);
		fl->add(data + 5, length - 5);
	} else {
		fl->add(data, length);
	}
}

static bool
build_compat(const char *xml, int xmllen, char *buf, int bufsize, int &flatlen)
{
	flatten fl = flatten(buf, bufsize - 1); // -1 to guarantee '\0' space
	XML_Parser parser = XML_ParserCreate(NULL);
	int rv;

	/*
	 * We parse the XML in order to flatten it.
	 * This does mean we have to parse it twice (once here
	 * for flattening, and again later for handling the
	 * event) but the XML parser runs reasonably fast.
	 *
	 * Note that if the parser returns with an error, we
	 * may have a partial result, e.g., if the initial <event>
	 * tag is improperly closed at the end we have a full and
	 * probably correct compat string.  However, we can't tell
	 * where things went wrong, so it's probably best just to
	 * have the caller log the issue and move on.
	 */
	flatlen = 0;
	XML_SetElementHandler(parser, fl_element, NULL);
	XML_SetCharacterDataHandler(parser, fl_data);
	XML_SetUserData(parser, &fl);
	rv = XML_Parse(parser, xml, xmllen, XML_TRUE);
	XML_ParserFree(parser);
	if (rv == XML_STATUS_ERROR)
		return true;
	if (fl.fl_len == 0)	// i.e., <event></event>
		return true;
	fl.finalize();
	// if (fl.fl_trunc)	// probably can't happen
	//	return true;
	flatlen = fl.fl_len;
	buf[flatlen] = '\0';
	return false;
}

static void
event_loop(void)
{
	int rv;
	int fd;
	char kern_buf[DEVCTL_MAXBUF];
	char flat_buf[DEVCTL_MAXBUF];
	int xml_len;
	int flat_len;
	int once = 0;
	int stream_fd_xml, stream_fd_compat;
	int seqpacket_fd_xml, seqpacket_fd_compat;
	int max_fd;
	int accepting;
	timeval tv;
	fd_set fds;

	fd = open(PATH_DEVCTL, O_RDONLY | O_CLOEXEC);
	if (fd == -1)
		err(1, "Can't open devctl device %s", PATH_DEVCTL);
	stream_fd_xml = create_socket(STREAMPIPE_XML, SOCK_STREAM);
	stream_fd_compat = create_socket(STREAMPIPE_COMPAT, SOCK_STREAM);
	seqpacket_fd_xml = create_socket(SEQPACKETPIPE_XML, SOCK_SEQPACKET);
	seqpacket_fd_compat = create_socket(SEQPACKETPIPE_COMPAT,
	    SOCK_SEQPACKET);
	accepting = 1;
	{
		// We could do a series of max(...) ops but this
		// seems a bit nicer.
		const int arr[] = {
			fd, stream_fd_xml, stream_fd_compat,
			seqpacket_fd_xml, seqpacket_fd_compat,
		};
		max_fd = (*max_element(begin(arr), end(arr))) + 1;
	}
	while (!romeo_must_die) {
		if (!once && !no_daemon && !daemonize_quick) {
			// Check to see if we have any events pending.
			tv.tv_sec = 0;
			tv.tv_usec = 0;
			FD_ZERO(&fds);
			FD_SET(fd, &fds);
			rv = select(fd + 1, &fds, &fds, &fds, &tv);
			// No events -> we've processed all pending events
			if (rv == 0) {
				devdlog(LOG_DEBUG, "Calling daemon\n");
				cfg.remove_pidfile();
				cfg.open_pidfile();
				daemon(0, 0);
				cfg.write_pidfile();
				once++;
			}
		}
		/*
		 * When we've already got the max number of clients, stop
		 * accepting new connections (don't put the listening sockets in
		 * the set), shrink the accept() queue to reject connections
		 * quickly, and poll the existing clients more often, so that we
		 * notice more quickly when any of them disappear to free up
		 * client slots.
		 */
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		if (num_clients < max_clients) {
			if (!accepting) {
				listen(stream_fd_xml, max_clients);
				listen(stream_fd_compat, max_clients);
				listen(seqpacket_fd_xml, max_clients);
				listen(seqpacket_fd_compat, max_clients);
				accepting = 1;
			}
			FD_SET(stream_fd_xml, &fds);
			FD_SET(stream_fd_compat, &fds);
			FD_SET(seqpacket_fd_xml, &fds);
			FD_SET(seqpacket_fd_compat, &fds);
			tv.tv_sec = 60;
			tv.tv_usec = 0;
		} else {
			if (accepting) {
				listen(stream_fd_xml, 0);
				listen(stream_fd_compat, 0);
				listen(seqpacket_fd_xml, 0);
				listen(seqpacket_fd_compat, 0);
				accepting = 0;
			}
			tv.tv_sec = 2;
			tv.tv_usec = 0;
		}
		rv = select(max_fd, &fds, NULL, NULL, &tv);
		if (got_siginfo) {
			devdlog(LOG_NOTICE, "Events received so far=%u\n",
			    total_events);
			got_siginfo = 0;
		}
		if (rv == -1) {
			if (errno == EINTR)
				continue;
			err(1, "select");
		} else if (rv == 0)
			check_clients();
		if (FD_ISSET(fd, &fds)) {
			rv = read(fd, kern_buf, sizeof(kern_buf) - 1);
			if (rv > 0) {
				total_events++;
				if (rv == sizeof(kern_buf) - 1) {
					devdlog(LOG_WARNING, "Warning: "
					    "available event data exceeded "
					    "buffer space\n");
				}
				/*
				 * kern_buf should end with "\n\0";
				 * make sure it does at least end
				 * with '\0'.  The sizeof...-1 above
				 * left room to guarantee it.
				 * However, XML parser can't abide \0
				 * and we want to syslog without the \n
				 * in case of error below, so back
				 * them off in the build_compat arg.
				 */
				xml_len = rv;
				if (kern_buf[rv - 1] == '\0') {
					xml_len = rv - 1;
					if (xml_len > 0 &&
					    kern_buf[xml_len - 1] == '\n')
						xml_len--;
				} else {
					kern_buf[rv] = '\0';
				}
				if (build_compat(kern_buf, xml_len,
				    flat_buf, sizeof(flat_buf),
				    flat_len)) {
					devdlog(LOG_ERR, "Error: "
					    "incoming event '%.*s' could not "
					    "be parsed as XML\n",
					    xml_len, kern_buf);
				} else {
					// NB: XML clients get \n\0 too
					notify_clients(kern_buf, rv,
					    flat_buf, flat_len);
					// If we're debugging, use an
					// extra check_clients to scan
					// the relative queue depths.
					if (client_debug >= 0)
						check_clients();
					process_event(kern_buf);
				}
			} else if (rv < 0) {
				if (errno != EINTR)
					break;
			} else {
				/* EOF */
				break;
			}
		}
		if (FD_ISSET(stream_fd_xml, &fds))
			new_client(stream_fd_xml, true, SOCK_STREAM);
		if (FD_ISSET(stream_fd_compat, &fds))
			new_client(stream_fd_compat, false, SOCK_STREAM);
		/*
		 * Aside from the socket type, both sockets use the same
		 * protocol, so we can process clients the same way.
		 */
		if (FD_ISSET(seqpacket_fd_xml, &fds))
			new_client(seqpacket_fd_xml, true, SOCK_SEQPACKET);
		if (FD_ISSET(seqpacket_fd_compat, &fds))
			new_client(seqpacket_fd_compat, false, SOCK_SEQPACKET);
	}
	close(seqpacket_fd_xml);
	close(stream_fd_compat);
	close(fd);
}

/*
 * functions that the parser uses.
 */
void
add_attach(int prio, event_proc *p)
{
	cfg.add_attach(prio, p);
}

void
add_detach(int prio, event_proc *p)
{
	cfg.add_detach(prio, p);
}

void
add_directory(const char *dir)
{
	cfg.add_directory(dir);
	free(const_cast<char *>(dir));
}

void
add_nomatch(int prio, event_proc *p)
{
	cfg.add_nomatch(prio, p);
}

void
add_notify(int prio, event_proc *p)
{
	cfg.add_notify(prio, p);
}

event_proc *
add_to_event_proc(event_proc *ep, eps *eps)
{
	if (ep == NULL)
		ep = new event_proc();
	ep->add(eps);
	return (ep);
}

eps *
new_action(const char *cmd)
{
	eps *e = new action(cmd);
	free(const_cast<char *>(cmd));
	return (e);
}

eps *
new_match(const char *var, const char *re)
{
	eps *e = new match(cfg, var, re);
	free(const_cast<char *>(var));
	free(const_cast<char *>(re));
	return (e);
}

eps *
new_media(const char *var, const char *re)
{
	eps *e = new media(cfg, var, re);
	free(const_cast<char *>(var));
	free(const_cast<char *>(re));
	return (e);
}

void
set_pidfile(const char *name)
{
	cfg.set_pidfile(name);
	free(const_cast<char *>(name));
}

void
set_variable(const char *var, const char *val)
{
	cfg.set_variable(var, val);
	free(const_cast<char *>(var));
	free(const_cast<char *>(val));
}



static void
gensighand(int)
{
	romeo_must_die = 1;
}

/*
 * SIGINFO handler.  Will print useful statistics to the syslog or stderr
 * as appropriate
 */
static void
siginfohand(int)
{
	got_siginfo = 1;
}

/*
 * Local logging function.  Prints to syslog if we're daemonized; stderr
 * otherwise.
 */
static void
devdlog(int priority, const char* fmt, ...)
{
	va_list argp;

	va_start(argp, fmt);
	if (no_daemon)
		vfprintf(stderr, fmt, argp);
	else if ((! quiet_mode) || (priority <= LOG_WARNING))
		vsyslog(priority, fmt, argp);
	va_end(argp);
}

static void
usage()
{
	fprintf(stderr, "usage: %s [-dnq] [-l connlimit] [-f file] "
	    " [-B size[,nrec]] [-D level]\n",
	    getprogname());
	exit(1);
}

static void
check_devd_enabled()
{
	int val = 0;
	size_t len;

	len = sizeof(val);
	if (sysctlbyname(SYSCTL, &val, &len, NULL, 0) != 0)
		errx(1, "devctl sysctl missing from kernel!");
	if (val == 0) {
		warnx("Setting " SYSCTL " to 1000");
		val = 1000;
		if (sysctlbyname(SYSCTL, NULL, NULL, &val, sizeof(val)))
			err(1, "sysctlbyname");
	}
}

static void
get_client_buffer_size(const char *arg)
{
	const char *commap;
	size_t size, nrec;

	size = strtoul(arg, NULL, 0);
	commap = strchr(arg, ',');
	if (commap != NULL) {
		nrec = strtoul(commap + 1, NULL, 0);
	} else {
		/*
		 * Maintain the default ratio.
		 */
		nrec = size / (client_bufsize / client_maxrecs);
		nrec = MAX(nrec, 1);
	}
	if (size == 0 || nrec == 0) {
		fprintf(stderr, "invalid -B argument %s: "
		    "size and nrec must be > 0\n", arg);
		usage();
	}
	client_bufsize = size;
	client_maxrecs = nrec;
}

static int
get_debug_level(const char *arg)
{
#ifdef notdef // requires c++11 (and #include <unordered_map>)
	static std::unordered_map<std::string,int> loglevels = {
		{ "DEBUG", LOG_DEBUG },
		{ "INFO", LOG_INFO },
		{ "WARNING", LOG_WARNING },
		{ "NOTICE", LOG_NOTICE },
		{ "ERR", LOG_ERR },
	};
	int level = 0;

	try {
		level = loglevels.at(arg);
	} catch (std::out_of_range) {
		fprintf(stderr, "%s: invalid debug level\n", arg);
		fprintf(stderr, "valid levels are:\n");
		for (auto& x: loglevels)
			fprintf(stderr, "   %s = %d\n",
			    x.first.c_str(), x.second);
		usage();
	}
	return level;
#else
	static struct loglevel {
		const char *name;
		int level;
	} loglevels[] = {
		{ "DEBUG", LOG_DEBUG },
		{ "INFO", LOG_INFO },
		{ "WARNING", LOG_WARNING },
		{ "NOTICE", LOG_NOTICE },
		{ "ERR", LOG_ERR },
		{ NULL, 0 }
	};
	struct loglevel *p;

	for (p = loglevels; p->name != NULL; p++)
		if (strcasecmp(arg, p->name) == 0)
			break;
	if (p->name == NULL) {
		fprintf(stderr, "%s: invalid debug level\n", arg);
		fprintf(stderr, "valid levels are:\n");
		for (p = loglevels; p->name != NULL; p++)
			fprintf(stderr, "   %s = %d\n", p->name, p->level);
		usage();
	}
	return p->level;
#endif
}

/*
 * main
 */
int
main(int argc, char **argv)
{
	int ch;

	check_devd_enabled();
	while ((ch = getopt(argc, argv, "B:D:df:l:nq")) != -1) {
		switch (ch) {
		case 'B':
			get_client_buffer_size(optarg);
			break;
		case 'D':
			client_debug = get_debug_level(optarg);
			break;
		case 'd':
			no_daemon = 1;
			break;
		case 'f':
			configfile = optarg;
			break;
		case 'l':
			max_clients = MAX(1, strtoul(optarg, NULL, 0));
			break;
		case 'n':
			daemonize_quick = 1;
			break;
		case 'q':
			quiet_mode = 1;
			break;
		default:
			usage();
		}
	}

	if (!no_daemon)
		openlog("devd", 0, LOG_DAEMON);
	cfg.parse();
	if (!no_daemon && daemonize_quick) {
		cfg.open_pidfile();
		daemon(0, 0);
		cfg.write_pidfile();
	}
	signal(SIGPIPE, SIG_IGN);
	signal(SIGHUP, gensighand);
	signal(SIGINT, gensighand);
	signal(SIGTERM, gensighand);
	signal(SIGINFO, siginfohand);
	event_loop();
	return (0);
}
