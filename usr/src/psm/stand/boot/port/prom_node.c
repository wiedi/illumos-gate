/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2017 Hayashi Naoyuki
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/promif.h>
#include <sys/promimpl.h>
#include <sys/systm.h>
#include <sys/salib.h>
#include <sys/bootvfs.h>
#include <util/getoptstr.h>
#include "boot_plat.h"
#include <libfdt.h>
#include <sys/platnames.h>
#include <stdbool.h>

static struct fdt_header *fdtp;


static pnode_t
fdt_offset_node(int offset)
{
	if (offset < 0)
		return (0);
	return ((phandle_t)offset + fdt_off_dt_struct(fdtp));
}

static int
fdt_node_offset(pnode_t p)
{
	int pint = (int)p;
	int dtoff = fdt_off_dt_struct(fdtp);

	if (pint < dtoff)
		return (-1);
	return (pint - dtoff);
}

pnode_t
prom_findnode_by_phandle(phandle_t phandle)
{
	int offset = fdt_node_offset_by_phandle(fdtp, phandle);
	if (offset < 0)
		return -1;
	phandle_t node = fdt_offset_node(offset);
	if (node < 0) {
		return OBP_BADNODE;
	}
	return node;
}

int
prom_getprop(pnode_t nodeid, const char *name, caddr_t value)
{
	int offset = fdt_node_offset(nodeid);
	if (offset < 0)
		return -1;

	int len;
	const void *prop = fdt_getprop(fdtp, offset, name, &len);

	if (prop == NULL) {
		if (strcmp(name, "name") == 0) {
			strcpy(value, fdt_get_name(fdtp, offset, &len));
			return len + 1;
		}
		return -1;
	}

	memcpy(value, prop, len);
	return len;
}

int
prom_setprop(pnode_t nodeid, const char *name, const caddr_t value, int len)
{
	int offset = fdt_node_offset(nodeid);
	if (offset < 0)
		return -1;

	if (strcmp(name, "name") == 0) {
		fdt_set_name(fdtp, offset, value);
	} else {
		fdt_setprop(fdtp, offset, name, value, len);
	}

	return len;
}

int
prom_getproplen(pnode_t nodeid, const char *name)
{
	int offset = fdt_node_offset(nodeid);
	if (offset < 0)
		return -1;

	int len;
	const struct fdt_property *prop = fdt_get_property(fdtp, offset, name, &len);

	if (prop == NULL) {
		if (strcmp(name, "name") == 0) {
			fdt_get_name(fdtp, offset, &len);
			return len + 1;
		}
		return  -1;
	}

	return len;
}

pnode_t
prom_finddevice(const char *device)
{
	int offset = fdt_path_offset(fdtp, device);
	if (offset < 0)
		return OBP_BADNODE;

	pnode_t node = fdt_offset_node(offset);
	if (node < 0) {
		return OBP_BADNODE;
	}
	return node;
}

pnode_t
prom_rootnode(void)
{
	pnode_t root = prom_finddevice("/");
	if (root < 0) {
		return OBP_NONODE;
	}
	return root;
}

pnode_t
prom_chosennode(void)
{
	pnode_t node = prom_finddevice("/chosen");
	if (node != OBP_BADNODE)
		return node;
	return OBP_NONODE;
}

char *
prom_nextprop(pnode_t nodeid, const char *name, char *next)
{
	int offset = fdt_node_offset(nodeid);
	if (offset < 0) {
		return NULL;
	}

	offset = fdt_first_property_offset(fdtp, offset);
	if (offset < 0) {
		return NULL;
	}

	const struct fdt_property *data;
	for (;;) {
		data = fdt_get_property_by_offset(fdtp, offset, NULL);
		const char* name0 = fdt_string(fdtp, fdt32_to_cpu(data->nameoff));
		if (name0) {
			if (*name == '\0') {
				strcpy(next, name0);
				return next;
			}
			if (strcmp(name, name0) == 0)
				break;
		}
		offset = fdt_next_property_offset(fdtp, offset);
		if (offset < 0) {
			return NULL;
		}
	}
	offset = fdt_next_property_offset(fdtp, offset);
	if (offset < 0) {
		return NULL;
	}
	data = fdt_get_property_by_offset(fdtp, offset, NULL);
	strcpy(next, (char*)fdt_string(fdtp, fdt32_to_cpu(data->nameoff)));
	return next;
}

pnode_t
prom_nextnode(pnode_t nodeid)
{
	if (nodeid == OBP_NONODE)
		return prom_rootnode();

	int offset = fdt_node_offset(nodeid);
	if (offset < 0)
		return OBP_BADNODE;

	int depth = 1;
	for (;;) {
		offset = fdt_next_node(fdtp, offset, &depth);
		if (offset < 0)
			return OBP_NONODE;
		if (depth == 1)
			return fdt_offset_node(offset);
	}
}

pnode_t
prom_childnode(pnode_t nodeid)
{
	int offset = fdt_node_offset(nodeid);
	if (offset < 0)
		return OBP_NONODE;

	int depth = 0;
	for (;;) {
		offset = fdt_next_node(fdtp, offset, &depth);
		if (offset < 0)
			return OBP_NONODE;
		if (depth == 0)
			return OBP_NONODE;
		if (depth == 1)
			return fdt_offset_node(offset);
	}
}

pnode_t
prom_parentnode(pnode_t nodeid)
{
	int offset = fdt_node_offset(nodeid);
	if (offset < 0)
		return OBP_NONODE;

	int parent_offset = fdt_parent_offset(fdtp, offset);
	if (parent_offset < 0)
		return OBP_NONODE;

	return fdt_offset_node(parent_offset);
}

char *
prom_decode_composite_string(void *buf, size_t buflen, char *prev)
{
	if ((buf == 0) || (buflen == 0) || ((int)buflen == -1))
		return ((char *)0);

	if (prev == 0)
		return ((char *)buf);

	prev += strlen(prev) + 1;
	if (prev >= ((char *)buf + buflen))
		return ((char *)0);
	return (prev);
}

int
prom_bounded_getprop(pnode_t nodeid, char *name, caddr_t value, int len)
{
	int prop_len = prom_getproplen(nodeid, name);
	if (prop_len < 0 || len < prop_len) {
		return -1;
	}

	return prom_getprop(nodeid, name, value);
}

char *
prom_bootpath(void)
{
	static char bootpath[OBP_MAXPATHLEN];
	int length;
	pnode_t node;
	static char *name = "__bootpath";

	if (bootpath[0] != 0)
		return bootpath;

	node = prom_chosennode();
	if (node == OBP_NONODE || node == OBP_BADNODE)
		return "";
	length = prom_getproplen(node, name);
	if (length == -1 || length == 0)
		return "";
	prom_getprop(node, name, bootpath);
	return bootpath;
}

char *
prom_bootargs(void)
{
	int length;
	pnode_t node;
	static char *name = "bootargs";
	static char bootargs[OBP_MAXPATHLEN];

	if (bootargs[0] != 0)
		return bootargs;

	node = prom_chosennode();
	if ((node == OBP_NONODE) || (node == OBP_BADNODE))
		return "";
	length = prom_getproplen(node, name);
	if (length == -1 || length == 0)
		return "";
	prom_getprop(node, name, bootargs);
	return bootargs;
}

pnode_t
prom_add_subnode(pnode_t parent, const char *name)
{
	return fdt_offset_node(fdt_add_subnode(fdtp, fdt_node_offset(parent), name));
}

int
prom_devname_from_pathname(char *pathname, char *buffer)
{
	char *p;

	if (pathname == NULL || *pathname == 0)
		return -1;

	p = prom_strrchr(pathname, '/');
	if (p == 0)
		return -1;

	p++;
	while (*p != 0)  {
		*buffer++ = *p++;
		if ((*p == '@') || (*p == ':'))
			break;
	}
	*buffer = 0;

	return 0;
}

void *get_fdtp(void)
{
	return fdtp;
}

void set_fdtp(void *p)
{
	fdtp = p;
}

static void
prom_node_dump(pnode_t nodeid, int depth)
{
	int namelen = prom_getproplen(nodeid, "name");
	if (namelen) {
		char *name = __builtin_alloca(namelen + 1);
		prom_getprop(nodeid, "name", name);
		for (int i = 0; i < depth; i++)
			prom_printf("    ");
		prom_printf("%s\n", name);
	}
	pnode_t child = prom_childnode(nodeid);
	while (child > 0) {
		prom_node_dump(child, depth + 1);
		child = prom_nextnode(child);
	}
}

void
prom_dump_tree(void)
{
	prom_node_dump(prom_rootnode(), 0);
}
