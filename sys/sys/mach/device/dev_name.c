/*
 * Copyright 1991-1998 by Open Software Foundation, Inc. 
 *              All Rights Reserved 
 *  
 * Permission to use, copy, modify, and distribute this software and 
 * its documentation for any purpose and without fee is hereby granted, 
 * provided that the above copyright notice appears in all copies and 
 * that both the copyright notice and this permission notice appear in 
 * supporting documentation. 
 *  
 * OSF DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE 
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS 
 * FOR A PARTICULAR PURPOSE. 
 *  
 * IN NO EVENT SHALL OSF BE LIABLE FOR ANY SPECIAL, INDIRECT, OR 
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM 
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN ACTION OF CONTRACT, 
 * NEGLIGENCE, OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION 
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. 
 */
/*
 * MkLinux
 */
/* CMU_HIST */
/*
 * Revision 2.5  91/07/31  17:32:50  dbg
 * 	Fixed to not modify the string passed in.
 * 	[91/07/23            dbg]
 * 
 * Revision 2.4  91/05/14  15:41:13  mrt
 * 	Correcting copyright
 * 
 * Revision 2.3  91/02/05  17:08:34  mrt
 * 	Changed to new Mach copyright
 * 	[91/01/31  17:27:34  mrt]
 * 
 * Revision 2.2  89/09/08  11:23:17  dbg
 * 	Moved device-name search routines here from dev_lookup.c
 * 	[89/08/01            dbg]
 * 
 */
/* CMU_ENDHIST */
/* 
 * Mach Operating System
 * Copyright (c) 1991,1990,1989 Carnegie Mellon University
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 * 
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 * 
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */
/*
 */
/*
 *	Author: David B. Golub, Carnegie Mellon University
 *	Date: 	8/89
 */

#include <string.h>
#include <device/device_types.h>
#include <device/dev_hdr.h>
#include <device/conf.h>
#include <device/ds_routines.h>
#include <cdli.h>

#if CDLI > 0
#include <device/cdli.h>
#endif

dev_ops_t	*dev_ops_table;
int		dev_ops_size;
dev_indirect_t	dev_indirect_table;
int		dev_indirect_size;
int		dev_indirect_max;

decl_simple_lock_data(,dev_name_lock)

/* Forward */

extern boolean_t	name_equal(
				char		* src,
				int		len,
				char		* target);

/*
 * Routines placed in empty entries in the device tables
 */
int
nulldev(void)
{
	return (D_SUCCESS);
}

int
nodev(void)
{
	return (D_INVALID_OPERATION);
}


/*
 * Name comparison routine.
 * Compares first 'len' characters of 'src'
 * with 'target', which is zero-terminated.
 * Returns TRUE if strings "match":
 *   src and target are equal or
 *   src and target are equal in first 'len' characters and
 *     next character of target is 0 (end of string).
 */
boolean_t
name_equal(
	char		* src,
	int		len,
	char		* target)
{
	while (--len >= 0)
	    if (*src++ != *target++)
		return FALSE;
	if (*target == 0)
	    return TRUE;
	while (*src)
	    if (*src++ != *target++)
		return FALSE;
	return *target == 0;
}

/*
 * device name lookup
 */
boolean_t
dev_name_lookup(
	char		* name,
	dev_ops_t	*ops,	/* out */
	int		*unit)	/* out */
{
	/*
	 * Assume that block device names are of the form
	 *
	 * <device_name><unit_number><partition>
	 *
	 * where
	 * <device_name>	is the name in the device table
	 * <unit_number>	is an integer
	 * <partition>		is a letter in [a-h]
	 */

	register char *	cp = name;
	int		len, lenunit;
	register int	j = 0;
	register int	c;
	dev_ops_t	dev;
	dev_ops_t     * devp;
	register boolean_t found;

	/*
	 * Find device type name (characters before digit)
	 */
	while ((c = *cp) != '\0' &&
		!(c >= '0' && c <= '9'))
	    cp++;

	len = cp - name;
	if (c != '\0') {
	    /*
	     * Find unit number
	     */
	    while ((c = *cp) != '\0' &&
		    c >= '0' && c <= '9') {
		j = j * 10 + (c - '0');
		cp++;
	    }
	}

	lenunit = cp - name;

	found = FALSE;
	simple_lock(&dev_name_lock);
	for (devp = dev_ops_table;
	     devp < dev_ops_table + dev_ops_size;
	     devp++) {
	    dev = *devp;
	    if (name_equal(name, len, dev->d_name)) {
		found = TRUE;
		break;
	    }
	}
	if (!found) {
	    /* name not found - try indirection list */
	    register dev_indirect_t	di;

	    /*
	     * We want console0 to match console,
	     * and we want disk0 to match disk0,
	     * but we don't want disk1 to match disk0.
	     */
	    for (di = dev_indirect_table;
		 di < dev_indirect_table + dev_indirect_size;
		 di++) {

#if SCSI_SEQUENTIAL_DISKS
		if (name_equal(name, len, di->d_name)
		|| (len != lenunit && name_equal(name, lenunit, di->d_name)))
#else
		if (name_equal(name, len, di->d_name))
#endif
		{
		    /*
		     * Return device and unit from indirect vector.
		     */
		    *ops = di->d_ops;
		    *unit = di->d_unit;

#if SCSI_SEQUENTIAL_DISKS
		    if (di->d_ops->d_subdev  > 0 && c >= 'a' && c < 'a'+di->d_ops->d_subdev)
			/* Note the sub-unit has already
			 * been computed.. simply add in
			 * the partition number
			 */
			*unit = *unit + (c - 'a');
#endif
		    simple_unlock(&dev_name_lock);
		    return (TRUE);
		}
	    }
	    simple_unlock(&dev_name_lock);
#if CDLI > 0
    {
	    /* Check CDLI for device */
	    struct ndd *nddp;
	    
	    if (nddp = ns_locate_wo_lock(name))
	    {
		    *ops = &nddp->d_ops;
		    *unit = nddp->ndd_unit;
		    return (TRUE);
	    }
    }
#endif
	    /* Not found in either list. */
	    return (FALSE);
	}
	simple_unlock(&dev_name_lock);

	*ops = dev;
	*unit = j;

	/*
	 * Find sub-device number
	 */
	j = dev->d_subdev;
	if (j > 0) {
#ifdef	SQT
	    /*
	     * SQT block device names are of the form
	     *
	     * <device_name><unit_number>s<partition>
	     * i.e. wd1s2 (device wd, unit 1, partition 2)
	     *
	     * where
	     * <device_name>        is the name in the device table
	     * <unit_number>        is an integer
	     * s                    is the SQT separator
	     * <partition>          is an integer
	     */
	    int i = 0;
	    cp++;	/* Skip over the separator charater 's' */
            while ((c = *cp) != '\0' &&
                    c >= '0' && c <= '9') {
                i = i * 10 + (c - '0');
                cp++;
            }
           *unit = (*unit * j) + i;	/* Minor = <subdev_count>*unit+part */
#else	/* SQT */
	    if (c >= 'a' && c < 'a' + j) {
		/*
		 * Minor number is <subdev_count>*unit + letter.
		 */
		*unit = *unit * j + (c - 'a');
	    }
	    else {
		/*
		 * Assume unit A.
		 */
		*unit = *unit * j;
	    }
#endif	/* SQT */
	}
	return (TRUE);
}

/*
 * Change an entry in the indirection list.
 */
void
dev_set_indirection(
	char		*name,
	dev_ops_t	ops,
	int		unit)
{
	register dev_indirect_t di;
	char *n;

	n = (char *) kalloc(strlen(name) + 1);
	simple_lock(&dev_name_lock);
again:
	for (di = dev_indirect_table;
	     di < dev_indirect_table + dev_indirect_size;
	     di++) {
	    if (!strcmp(di->d_name, name)) {
		di->d_ops = ops;
		di->d_unit = unit;
		simple_unlock(&dev_name_lock);
		kfree((vm_offset_t) n, strlen(name) + 1);
		return;
	    }
	}
	if (dev_indirect_size == dev_indirect_max) {
	    dev_indirect_t otable;
	    int max = dev_indirect_max << 1;
	    int i;

	    simple_unlock(&dev_name_lock);
	    di = (dev_indirect_t) kalloc(max * sizeof(struct dev_indirect));
	    simple_lock(&dev_name_lock);
	    if (max != (dev_indirect_max << 1)) {
		kfree((vm_offset_t) di, max * sizeof(struct dev_indirect));
		goto again;
	    }
	    dev_indirect_max = max;
	    for (i = 0; i < dev_indirect_size; i++)
		di[i] = dev_indirect_table[i];
	    otable = dev_indirect_table;
	    dev_indirect_table = di;
    	    simple_unlock(&dev_name_lock);
	    max >>= 1;
	    kfree((vm_offset_t) otable, max * sizeof(struct dev_indirect));
	    simple_lock(&dev_name_lock);
	    goto again;
	}
	di->d_name = n;
	strcpy(di->d_name, name);
	di->d_ops = ops;
	di->d_unit = unit;
	dev_indirect_size++;
	simple_unlock(&dev_name_lock);
}

/*
 * Lookup the indirect name of a given device based on
 * the devops and unit
 */

boolean_t
dev_find_indirect(dev_ops_t devops, int unit, char *realname)
{
	dev_indirect_t	di;
	int	i;
	
	for (di = dev_indirect_table, i = 0; i < dev_indirect_size; di++, i++) {
	    if (di->d_ops == devops && unit == di->d_unit) {
		strcpy(realname, di->d_name);
		return TRUE;
	    }
	}

	return FALSE;
}

void
dev_name_init(void)
{
    int max;
    int i;

    simple_lock_init(&dev_name_lock, ETAP_IO_DEV_NAME);

    dev_ops_size = dev_name_count;
    dev_ops_table = (dev_ops_t *) kalloc(dev_ops_size * sizeof(dev_ops_t));
    for (i = 0; i < dev_ops_size; i++)
	dev_ops_table[i] = &dev_name_list[i];

    dev_indirect_size = dev_indirect_count;
    max = 4;
    while (max <= dev_indirect_size)
	max <<= 1;
    dev_indirect_max = max;
    dev_indirect_table =
	(dev_indirect_t) kalloc(max * sizeof(struct dev_indirect));
    for (i = 0; i < dev_indirect_size; i++)
	dev_indirect_table[i] = dev_indirect_list[i];
}
