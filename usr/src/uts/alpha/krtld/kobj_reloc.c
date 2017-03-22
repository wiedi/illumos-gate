/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */


#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/bootconf.h>
#include <sys/modctl.h>
#include <sys/elf.h>
#include <sys/kobj.h>
#include <sys/kobj_impl.h>

#include "reloc.h"

#define	SDT_NOP		0x47ff041f

void do_nothing(void)
{
}

int
sdt_reloc_resolve(struct module *mp, char *symname, void *instr, long roff, uint_t rtype)
{
	sdt_probedesc_t *sdp;
	int i;

	/*
	 * The "statically defined tracing" (SDT) provider for DTrace uses
	 * a mechanism similar to TNF, but somewhat simpler.  (Surprise,
	 * surprise.)  The SDT mechanism works by replacing calls to the
	 * undefined routine __dtrace_probe_[name] with nop instructions.
	 * The relocations are logged, and SDT itself will later patch the
	 * running binary appropriately.
	 */
	if (strncmp(symname, sdt_prefix, strlen(sdt_prefix)) != 0)
		return (1);

	ASSERT(rtype == R_ALPHA_JMP_SLOT);
	symname += strlen(sdt_prefix);

	sdp = kobj_alloc(sizeof (sdt_probedesc_t), KM_WAIT);
	sdp->sdpd_name = kobj_alloc(strlen(symname) + 1, KM_WAIT);
	bcopy(symname, sdp->sdpd_name, strlen(symname) + 1);

	if (roff == (long)instr) {
		/*
		 * This isn't an offset -- it's an absolute value.  (This is
		 * typically only true for "unix".)  We need to convert the
		 * value into an offset from mp->text.
		 */
		roff -= (uintptr_t)mp->text;
	}

	sdp->sdpd_offset = (uintptr_t)instr;
	sdp->sdpd_next = mp->sdt_probes;
	mp->sdt_probes = sdp;

	/* Install NOP at probe point. */
	*(uint64_t *)instr = (uint64_t)do_nothing;

	return (0);
}

int
/* ARGSUSED2 */
do_relocate(struct module *mp, char *reltbl, Word relshtype, int nreloc,
	int relocsize, Addr baseaddr)
{
	Word stndx;
	long off, roff;
	uintptr_t reladdr, rend;
	uint_t rtype;
	Elf64_Sxword addend;
	Addr value, destination;
	Sym *symref;
	int symnum;
	int err = 0;
	char *name = "";

	reladdr = (uintptr_t)reltbl;
	rend = reladdr + nreloc * relocsize;

#ifdef	KOBJ_DEBUG
	if (kobj_debug & D_RELOCATIONS) {
		_kobj_printf(ops, "krtld:\ttype\t\t\toffset\t   addend"
		    "      symbol\n");
		_kobj_printf(ops, "krtld:\t\t\t\t\t   value\n");
	}
#endif
	destination = baseaddr;

	symnum = -1;
	/* loop through relocations */
	while (reladdr < rend) {

		symnum++;
		rtype = ELF_R_TYPE(((Rela *)reladdr)->r_info);
		roff = off = ((Rela *)reladdr)->r_offset;
		stndx = ELF_R_SYM(((Rela *)reladdr)->r_info);
		if (stndx >= mp->nsyms) {
			_kobj_printf(ops,
			    "do_relocate: bad strndx %d\n", symnum);
			return (-1);
		}
		if ((rtype > R_ALPHA_NUM) || IS_TLS_INS(rtype)) {
			_kobj_printf(ops, "krtld: invalid relocation type %d",
			    rtype);
			_kobj_printf(ops, " at 0x%llx:", off);
			_kobj_printf(ops, " file=%s\n", mp->filename);
			err = 1;
			continue;
		}
		addend = (long)(((Rela *)reladdr)->r_addend);
		reladdr += relocsize;

#ifdef	KOBJ_DEBUG
		if (kobj_debug & D_RELOCATIONS) {
			Sym *symp;
			symp = (Sym *)
			    (mp->symtbl+(stndx * mp->symhdr->sh_entsize));
			_kobj_printf(ops, "krtld:\t%s",
			    conv_reloc_ALPHA_type(rtype));
			_kobj_printf(ops, "\t0x%8llx", off);
			_kobj_printf(ops, " 0x%8llx", addend);
			_kobj_printf(ops, "  %s\n",
			    (const char *)mp->strings + symp->st_name);
		}
#endif

		if (rtype == R_ALPHA_NONE)
			continue;

		if (!(mp->flags & KOBJ_EXEC))
			off += destination;

		/*
		 * if R_ALPHA_RELATIVE, simply add base addr
		 * to reloc location
		 */
		if (rtype == R_ALPHA_RELATIVE) {
			value = baseaddr;
			name = "";
		} else {
			/*
			 * get symbol table entry - if symbol is local
			 * value is base address of this object
			 */
			symref = (Sym *)
			    (mp->symtbl+(stndx * mp->symhdr->sh_entsize));
			if (ELF_ST_BIND(symref->st_info) == STB_LOCAL) {
				/* *** this is different for .o and .so */
				value = symref->st_value;
				if (symref->st_shndx != SHN_ABS) {
					value += (uintptr_t)mp->dso;
				}
			} else {
				/*
				 * It's global. Allow weak references.  If
				 * the symbol is undefined, give TNF (the
				 * kernel probes facility) a chance to see
				 * if it's a probe site, and fix it up if so.
				 */
				if (symref->st_shndx == SHN_UNDEF &&
				    sdt_reloc_resolve(mp, mp->strings +
				    symref->st_name, (void *)off,
				    roff + ((uintptr_t)baseaddr -
				    (uintptr_t)mp->text), rtype) == 0)
					continue;

				/*
				 * calculate location of definition
				 * - symbol value plus base address of
				 * containing shared object
				 */
				value = symref->st_value;
				if (symref->st_shndx != SHN_ABS) {
					value += (uintptr_t)mp->dso;
				}
			}
			name = (char *)mp->strings + symref->st_name;
		} /* end not R_ALPHA_RELATIVE */

		if (rtype != R_ALPHA_JMP_SLOT) {
			value += addend;
		}

		/*
		 * calculate final value -
		 * if PC-relative, subtract ref addr
		 */
		if (IS_PC_RELATIVE(rtype)) {
			value -= off;
		}

#ifdef	KOBJ_DEBUG
		if (kobj_debug & D_RELOCATIONS) {
			_kobj_printf(ops, "krtld:\t\t\t\t0x%8llx", off);
			_kobj_printf(ops, " 0x%8llx\n", value);
		}
#endif
		if (do_reloc_krtld(rtype, (unsigned char *)off, (Xword *)&value,
		    name, mp->filename) == 0)
			err = 1;
	} /* end of while loop */

	if (err)
		return (-1);

	return (0);
}

int
do_relocations(struct module *mp)
{
	uint_t shn;
	Shdr *shp, *rshp;
	uint_t nreloc;

	/* do the relocations */
	for (shn = 1; shn < mp->hdr.e_shnum; shn++) {
		rshp = (Shdr *)
		    (mp->shdrs + shn * mp->hdr.e_shentsize);
		if (rshp->sh_type == SHT_REL) {
			_kobj_printf(ops, "%s can't process type SHT_REL\n",
			    mp->filename);
			return (-1);
		}
		if (rshp->sh_type != SHT_RELA)
			continue;
		if (rshp->sh_link != mp->symtbl_section) {
			_kobj_printf(ops, "%s reloc for non-default symtab\n",
			    mp->filename);
			return (-1);
		}
		if (rshp->sh_info >= mp->hdr.e_shnum) {
			_kobj_printf(ops, "do_relocations: %s ", mp->filename);
			_kobj_printf(ops, " sh_info out of range %d\n", shn);
			goto bad;
		}
		nreloc = rshp->sh_size / rshp->sh_entsize;

		/* get the section header that this reloc table refers to */
		shp = (Shdr *)
		    (mp->shdrs + rshp->sh_info * mp->hdr.e_shentsize);
		/*
		 * Do not relocate any section that isn't loaded into memory.
		 * Most commonly this will skip over the .rela.stab* sections
		 */
		if (!(shp->sh_flags & SHF_ALLOC))
			continue;
#ifdef	KOBJ_DEBUG
		if (kobj_debug & D_RELOCATIONS) {
			_kobj_printf(ops, "krtld: relocating: file=%s ",
			    mp->filename);
			_kobj_printf(ops, " section=%d\n", shn);
		}
#endif
		if (do_relocate(mp, (char *)rshp->sh_addr, rshp->sh_type,
		    nreloc, rshp->sh_entsize, shp->sh_addr) < 0) {
			_kobj_printf(ops,
			    "do_relocations: %s do_relocate failed\n",
			    mp->filename);
			goto bad;
		}
		kobj_free((void *)rshp->sh_addr, rshp->sh_size);
		rshp->sh_addr = 0;
	}
	mp->flags |= KOBJ_RELOCATED;
	return (0);
bad:
	kobj_free((void *)rshp->sh_addr, rshp->sh_size);
	rshp->sh_addr = 0;
	return (-1);
}
