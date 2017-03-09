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
 * Copyright (c) 1999 by Sun Microsystems, Inc.
 * All rights reserved.
 */
/*
 * Copyright 2017 Hayashi Naoyuki
 */

#ifndef	_SNOOP_MIP_H
#define	_SNOOP_MIP_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef __cplusplus
extern "C" {
#endif

#define	ALIGN(ptr)		(ptr)

/*
 *	E X T E N S I O N S
 */

typedef struct {
	uchar_t		type;
	uchar_t		length;
} exthdr_t;

/* This header is used for Generalized MIP Authentication Extensions */
typedef struct {
	uint8_t		type;
	uint8_t		subtype;
	uint16_t	length;
} gen_exthdr_t;

#define	MN_HA_AUTH	32
#define	MN_FA_AUTH	33
#define	FA_HA_AUTH	34
#define	GEN_AUTH	36
#define	MN_HA_KEY	126
#define	MN_HA_TRAVERSE	129
#define	ENCAP_DELIV	130
#define	MN_NAI		131
#define	FA_CHALLENGE	132
#define	MN_FA_KEY	133

/* Subtypes for Generalized MIP Authentication Extension (GEN_AUTH) */
#define	GEN_AUTH_MN_AAA		1

#define	KEY_ALG_NONE		0
#define	SA_MD5_MODE_PREF_SUF	2	/* ...in prefix+suffix */
#define	SA_HMAC_MD5		3

/*
 * 	R E G I S T R A T I O N    P R O T O C O L
 */

#define	REG_TYPE_REQ	1
#define	REG_TYPE_REP	3

typedef struct ident_str {
	uint32_t high_bits;	/* generated by the HA */
	uint32_t low_bits;	/* generated by the MN */
} ident_t;

#ifdef __sparc
#ifdef _BIT_FIELDS_HTOL
typedef struct registration_request_str {
	uchar_t		type;		/* must be REG_TYPE_REQ */
	uchar_t
		Simultaneous_registration : 	1,
		Broadcasts_desired : 		1,
		Decapsulation_done_locally : 	1, /* ...by the popup MN */
		Minimal_encap_desired : 	1,
		GRE_encap_desired : 		1,
		VJ_compression_desired : 	1,
		BiDirectional_Tunnel_desired : 	1,
		reserved : 			1;
	ushort_t	lifetime;	/* 0 = dereg; 0xffff = infinity */
	in_addr_t	home_addr;	/* address of the MN */
	in_addr_t	home_agent_addr; /* address of a HA */
	in_addr_t	care_of_addr;	/* address of decap endpoint */
	ident_t		identification;	/* for replay protection */
} regreq_t;
#endif /* _BIT_FIELDS_HTOL */
#endif /* __sparc */

#if defined(__alpha) || defined(__aarch64)
#ifdef _BIT_FIELDS_LTOH
typedef struct registration_request_str {
	uchar_t		type;		/* must be REG_TYPE_REQ */
	uchar_t
		reserved : 			1,
		BiDirectional_Tunnel_desired : 	1,
		VJ_compression_desired : 	1,
		GRE_encap_desired : 		1,
		Minimal_encap_desired : 	1,
		Decapsulation_done_locally : 	1, /* ...by the popup MN */
		Broadcasts_desired : 		1,
		Simultaneous_registration : 	1;
	ushort_t	lifetime;	/* 0 = dereg; 0xffff = infinity */
	in_addr_t	home_addr;	/* address of the MN */
	in_addr_t	home_agent_addr; /* address of a HA */
	in_addr_t	care_of_addr;	/* address of decap endpoint */
	ident_t		identification;	/* for replay protection */
} regreq_t;
#endif /* _BIT_FIELDS_LTOH */
#endif /* __alpha */

#ifdef __i386
#ifdef _BIT_FIELDS_LTOH
typedef struct registration_request_str {
	uchar_t		type;		/* must be REG_TYPE_REQ */
	uchar_t
		reserved : 			1,
		BiDirectional_Tunnel_desired : 	1,
		VJ_compression_desired : 	1,
		GRE_encap_desired : 		1,
		Minimal_encap_desired : 	1,
		Decapsulation_done_locally : 	1, /* ...by the popup MN */
		Broadcasts_desired : 		1,
		Simultaneous_registration : 	1;
	ushort_t	lifetime;	/* 0 = dereg; 0xffff = infinity */
	in_addr_t	home_addr;	/* address of the MN */
	in_addr_t	home_agent_addr; /* address of a HA */
	in_addr_t	care_of_addr;	/* address of decap endpoint */
	ident_t		identification;	/* for replay protection */
} regreq_t;
#endif /* _BIT_FIELDS_LTOH */
#endif /* __i386 */

/*
 * Registration Reply sent by a home agent to a mobile node in
 * response to a registration request.
 */
typedef struct registration_reply_str {
	uchar_t		type;		/* must be REG_TYPE_REP */
	uchar_t		code;		/* refer to draft document */
	ushort_t	lifetime;	/* 0 = dereg; 0xffff = infinity */
	in_addr_t	home_addr;	/* address of the mobile node */
	in_addr_t	home_agent_addr; /* address of the home agent */
	ident_t		identification;	/* derived from request's field */
} regrep_t;

/* service ok */
#define	REPLY_CODE_ACK					0
#define	REPLY_CODE_ACK_NO_SIMULTANEOUS			1

/* denied by FA */
#define	REPLY_CODE_FA_NACK_UNSPECIFIED			64
#define	REPLY_CODE_FA_NACK_PROHIBITED			65
#define	REPLY_CODE_FA_NACK_RESOURCES			66
#define	REPLY_CODE_FA_NACK_MN_AUTH			67
#define	REPLY_CODE_FA_NACK_HA_AUTH			68
#define	REPLY_CODE_FA_NACK_LIFETIME			69
#define	REPLY_CODE_FA_NACK_BAD_REQUEST			70
#define	REPLY_CODE_FA_NACK_BAD_REPLY			71
#define	REPLY_CODE_FA_NACK_ENCAP_UNAVAILABLE		72
#define	REPLY_CODE_FA_NACK_VJ_UNAVAILABLE		73
#define	REPLY_CODE_FA_NACK_BIDIR_TUNNEL_UNAVAILABLE	74
#define	REPLY_CODE_FA_NACK_BIDIR_TUNNEL_NO_TBIT		75
#define	REPLY_CODE_FA_NACK_BIDIR_TUNNEL_TOO_DISTANT	76
#define	REPLY_CODE_FA_NACK_ICMP_HA_NET_UNREACHABLE	80
#define	REPLY_CODE_FA_NACK_ICMP_HA_HOST_UNREACHABLE	81
#define	REPLY_CODE_FA_NACK_ICMP_HA_PORT_UNREACHABLE	82
#define	REPLY_CODE_FA_NACK_ICMP_HA_UNREACHABLE		88
#define	REPLY_CODE_FA_NACK_UNIQUE_HOMEADDR_REQD		96
#define	REPLY_CODE_FA_NACK_MISSING_NAI			97
#define	REPLY_CODE_FA_NACK_MISSING_HOME_AGENT		98
#define	REPLY_CODE_FA_NACK_MISSING_HOMEADDR		99
#define	REPLY_CODE_FA_NACK_UNKNOWN_CHALLENGE		104
#define	REPLY_CODE_FA_NACK_MISSING_CHALLENGE		105
#define	REPLY_CODE_FA_NACK_MISSING_MN_FA		106

/* denied by HA */
#define	REPLY_CODE_HA_NACK_UNSPECIFIED			128
#define	REPLY_CODE_HA_NACK_PROHIBITED			129
#define	REPLY_CODE_HA_NACK_RESOURCES			130
#define	REPLY_CODE_HA_NACK_MN_AUTH			131
#define	REPLY_CODE_HA_NACK_FA_AUTH			132
#define	REPLY_CODE_HA_NACK_ID_MISMATCH			133
#define	REPLY_CODE_HA_NACK_BAD_REQUEST			134
#define	REPLY_CODE_HA_NACK_TOO_MANY_BINDINGS		135
#define	REPLY_CODE_HA_NACK_BAD_HA_ADDRESS		136
#define	REPLY_CODE_HA_NACK_BIDIR_TUNNEL_UNAVAILABLE	137
#define	REPLY_CODE_HA_NACK_BIDIR_TUNNEL_NO_TBIT		138
#define	REPLY_CODE_HA_NACK_BIDIR_ENCAP_UNAVAILABLE	139

/*
 * OTHER EXTENSIONS
 */

/*
 * The second set consists of those extensions which may appear only
 * in ICMP Router Discovery messages [4].  Currently, Mobile IP
 * defines the following Types for Extensions appearing in ICMP
 * Router Discovery messages:
 *
 * 0  One-byte PaddingOne-byte Padding (encoded with no Length nor
 * Data field)
 * 16  Mobility Agent Advertisement
 * 19  Prefix-Lengths
 */
#define	ICMP_ADV_MSG_PADDING_EXT	0
#define	ICMP_ADV_MSG_MOBILITY_AGT_EXT	16
#define	ICMP_ADV_MSG_PREFIX_LENGTH_EXT	19
#define	ICMP_ADV_MSG_FA_CHALLENGE	24
#define	ICMP_ADV_MSG_FA_NAI		25


/*
 * Mobility Agent Advertisement Extension
 * The Mobility Agent Adv Extension follows the ICMP Router
 * Advertisement fields.It is used to indicate that an ICMP Router
 * Advertisement message is also an Agent Advertisement being sent
 * by a mobility agent.
 *
 * Type		16
 * Length	(6 + 4*N), where N is the number of care-of addresses
 *		advertised.
 *
 * Sequence Number
 * The count of Agent Advertisement messages sent since the
 * agent was initialized (Section 2.3.2).
 *
 * Registration Lifetime
 * The longest lifetime (measured in seconds) that this
 * agent is willing to accept in any Registration Request.
 * A value of 0xffff indicates infinity.  This field has no
 * relation to the "Lifetime" field within the ICMP Router
 * Advertisement portion of the Agent Advertisement.
 *
 * R	Registration required.  Registration with this foreign
 * agent (or another foreign agent on this link) is required
 * rather than using a co-located care-of address.
 *
 * B	Busy.  The foreign agent will not accept registrations
 * from additional mobile nodes.
 *
 * H	Home agent.  This agent offers service as a home agent
 * on the link on which this Agent Advertisement message is
 * sent.
 *
 * F	Foreign agent.  This agent offers service as a foreign
 * agent on the link on which this Agent Advertisement
 * message is sent.
 *
 * M	Minimal encapsulation.  This agent implements receiving
 * tunneled datagrams that use minimal encapsulation [15].
 *
 * G	GRE encapsulation.  This agent implements receiving
 * tunneled datagrams that use GRE encapsulation [8].
 *
 * V 	Van Jacobson header compression.  This agent supports use
 * of Van Jacobson header compression [10] over the link
 * with any registered mobile node.
 *
 * reserved 	sent as zero; ignored on reception.
 *
 * Care-of Address(es)
 * The advertised foreign agent care-of address(es) provided
 * by this foreign agent.  An Agent Advertisement MUST
 * include at least one care-of address if the 'F' bit
 * is set.  The number of care-of addresses present is
 * determined by the Length field in the Extension.
 *
 * A HA must always be prepared to serve the mobile nodes for
 * which it is the home agent. A FA may at times be too busy
 * to serve additional MNs; even so, it must continue to send
 * Agent Advertisements, so that any mobile nodes already registered
 * with it will know that they have not moved out of range of the
 * foreign agent and that the  has not failed.  A foreign
 * agent may indicate that it is "too busy" to allow new MNs to
 * register with it, by setting the 'B' bit in its Agent Adv.
 * An Agent Adv message MUST NOT have the 'B' bit set if the
 * 'F' bit is not also set, and at least one of the 'F' bit and the
 * 'H'  bit MUST be set in any Agent Advertisement message sent.
 *
 * When a FA wishes to require registration even from those
 * mobile nodes which have acquired a co-located care-of address, it
 * sets the 'R' bit to one. Because this bit applies only to foreign
 * agents, an agent MUST NOT set the 'R' bit to one unless the 'F'
 * bit is also set to one.
 */
#ifdef __sparc
#ifdef _BIT_FIELDS_HTOL
typedef struct mobility_agt_adv_extension {
	uchar_t		type;
	uchar_t		length;
	ushort_t	sequence_num;
	ushort_t	reg_lifetime;
	ushort_t	reg_bit:1,
			busy_bit:1,
			ha_bit:1,
			fa_bit:1,
			minencap_bit:1,
			greencap_bit:1,
			vanjacob_hdr_comp_bit:1,
			reverse_tunnel_bit:1,
			reserved:8;
} mobagtadvext_t;

#endif /* _BIT_FIELDS_HTOL */
#endif /* __sparc */

#ifdef __i386
#ifdef _BIT_FIELDS_LTOH
typedef struct mobility_agt_adv_extension {
	uchar_t		type;
	uchar_t		length;
	ushort_t	sequence_num;
	ushort_t	reg_lifetime;
	uchar_t
			reverse_tunnel_bit:1,
			vanjacob_hdr_comp_bit:1,
			greencap_bit:1,
			minencap_bit:1,
			fa_bit:1,
			ha_bit:1,
			busy_bit:1,
			reg_bit:1;
	uchar_t		reserved;
} mobagtadvext_t;
#endif /* _BIT_FIELDS_LTOH */
#endif /* __i386 */

#if defined(__alpha) || defined(__aarch64)
#ifdef _BIT_FIELDS_LTOH
typedef struct mobility_agt_adv_extension {
	uchar_t		type;
	uchar_t		length;
	ushort_t	sequence_num;
	ushort_t	reg_lifetime;
	uchar_t
			reverse_tunnel_bit:1,
			vanjacob_hdr_comp_bit:1,
			greencap_bit:1,
			minencap_bit:1,
			fa_bit:1,
			ha_bit:1,
			busy_bit:1,
			reg_bit:1;
	uchar_t		reserved;
} mobagtadvext_t;
#endif /* _BIT_FIELDS_LTOH */
#endif /* __i386 */
#ifdef __cplusplus
}
#endif

#endif /* _SNOOP_MIP_H */
