/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* SBR tables: QMF prototype filter, frequency-band offsets, Huffman tables.
 * All values are normative data from ISO/IEC 14496-3:2009 (Fourth Edition).
 * All SBR patents (Fraunhofer/Dolby) expired no later than 2021 in all
 * major jurisdictions. */

#ifndef SBR_TABLES_H
#define SBR_TABLES_H

#include <stdint.h>
#include "faac_real.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------
 * 640-tap QMF prototype filter (ISO 14496-3:2009 Table 4.A.90 / C.4)
 * Used for the 64-band encoder-side analysis QMF.
 * ------------------------------------------------------------------ */
static const faac_real qmf_c_compact[321] = {
    (faac_real)+0.0000000000e+00f, (faac_real)-5.5252865047e-04f, (faac_real)-5.6176925738e-04f, (faac_real)-4.9475180896e-04f,
    (faac_real)-4.8752279712e-04f, (faac_real)-4.8937912498e-04f, (faac_real)-5.0407143497e-04f, (faac_real)-5.2265642972e-04f,
    (faac_real)-5.4665656337e-04f, (faac_real)-5.6778025613e-04f, (faac_real)-5.8709304852e-04f, (faac_real)-6.1327473938e-04f,
    (faac_real)-6.3124935319e-04f, (faac_real)-6.5403333621e-04f, (faac_real)-6.7776907764e-04f, (faac_real)-6.9416146273e-04f,
    (faac_real)-7.1577364744e-04f, (faac_real)-7.2550431222e-04f, (faac_real)-7.4409418541e-04f, (faac_real)-7.4905980532e-04f,
    (faac_real)-7.6813719270e-04f, (faac_real)-7.7248485949e-04f, (faac_real)-7.8343322877e-04f, (faac_real)-7.7798694927e-04f,
    (faac_real)-7.8036647100e-04f, (faac_real)-7.8014496257e-04f, (faac_real)-7.7579773310e-04f, (faac_real)-7.6307935757e-04f,
    (faac_real)-7.5300014201e-04f, (faac_real)-7.3193571525e-04f, (faac_real)-7.2153919876e-04f, (faac_real)-6.9179375372e-04f,
    (faac_real)-6.6504150893e-04f, (faac_real)-6.3415949025e-04f, (faac_real)-5.9461189330e-04f, (faac_real)-5.5645763906e-04f,
    (faac_real)-5.1455722108e-04f, (faac_real)-4.6063254803e-04f, (faac_real)-4.0951214522e-04f, (faac_real)-3.5011758756e-04f,
    (faac_real)-2.8969811748e-04f, (faac_real)-2.0983373440e-04f, (faac_real)-1.4463809349e-04f, (faac_real)-6.1733440720e-05f,
    (faac_real)+1.3494974180e-05f, (faac_real)+1.0943831274e-04f, (faac_real)+2.0430170688e-04f, (faac_real)+2.9495311041e-04f,
    (faac_real)+4.0265402160e-04f, (faac_real)+5.1073884952e-04f, (faac_real)+6.2393761391e-04f, (faac_real)+7.4580258865e-04f,
    (faac_real)+8.6084433262e-04f, (faac_real)+9.8859883015e-04f, (faac_real)+1.1250155131e-03f, (faac_real)+1.2577884647e-03f,
    (faac_real)+1.3902494827e-03f, (faac_real)+1.5443219847e-03f, (faac_real)+1.6868083253e-03f, (faac_real)+1.8348265422e-03f,
    (faac_real)+1.9841140737e-03f, (faac_real)+2.1461583556e-03f, (faac_real)+2.3017254775e-03f, (faac_real)+2.4625616913e-03f,
    (faac_real)+2.6201758690e-03f, (faac_real)+2.7870464347e-03f, (faac_real)+2.9469447716e-03f, (faac_real)+3.1125420653e-03f,
    (faac_real)+3.2739613485e-03f, (faac_real)+3.4418874183e-03f, (faac_real)+3.6008268123e-03f, (faac_real)+3.7603922910e-03f,
    (faac_real)+3.9207432370e-03f, (faac_real)+4.0819753194e-03f, (faac_real)+4.2264269227e-03f, (faac_real)+4.3730719678e-03f,
    (faac_real)+4.5209852783e-03f, (faac_real)+4.6606460612e-03f, (faac_real)+4.7932560850e-03f, (faac_real)+4.9137603574e-03f,
    (faac_real)+5.0393022601e-03f, (faac_real)+5.1407353903e-03f, (faac_real)+5.2461166132e-03f, (faac_real)+5.3471681198e-03f,
    (faac_real)+5.4196775931e-03f, (faac_real)+5.4876040151e-03f, (faac_real)+5.5475714509e-03f, (faac_real)+5.5938023005e-03f,
    (faac_real)+5.6220643210e-03f, (faac_real)+5.6455196916e-03f, (faac_real)+5.6389199515e-03f, (faac_real)+5.6266114193e-03f,
    (faac_real)+5.5917128663e-03f, (faac_real)+5.5404363940e-03f, (faac_real)+5.4753783077e-03f, (faac_real)+5.3838975897e-03f,
    (faac_real)+5.2715758727e-03f, (faac_real)+5.1382275451e-03f, (faac_real)+4.9839687763e-03f, (faac_real)+4.8109469060e-03f,
    (faac_real)+4.6039530147e-03f, (faac_real)+4.3801861745e-03f, (faac_real)+4.1251642327e-03f, (faac_real)+3.8456408125e-03f,
    (faac_real)+3.5401246551e-03f, (faac_real)+3.2091885810e-03f, (faac_real)+2.8446757862e-03f, (faac_real)+2.4508540032e-03f,
    (faac_real)+2.0274176185e-03f, (faac_real)+1.5784682577e-03f, (faac_real)+1.0902329051e-03f, (faac_real)+5.8322642480e-04f,
    (faac_real)+2.7604519050e-05f, (faac_real)-5.4642808664e-04f, (faac_real)-1.1568135523e-03f, (faac_real)-1.8039472589e-03f,
    (faac_real)-2.4826723645e-03f, (faac_real)-3.1933778390e-03f, (faac_real)-3.9401124052e-03f, (faac_real)-4.7222596240e-03f,
    (faac_real)-5.5337211109e-03f, (faac_real)-6.3792293269e-03f, (faac_real)-7.2615816852e-03f, (faac_real)-8.1798233373e-03f,
    (faac_real)-9.1325329609e-03f, (faac_real)-1.0115021550e-02f, (faac_real)-1.1131554803e-02f, (faac_real)-1.2184999595e-02f,
    (faac_real)+1.3271822004e-02f, (faac_real)+1.4390466608e-02f, (faac_real)+1.5540555334e-02f, (faac_real)+1.6732471300e-02f,
    (faac_real)+1.7943338134e-02f, (faac_real)+1.9187243137e-02f, (faac_real)+2.0453179336e-02f, (faac_real)+2.1746755025e-02f,
    (faac_real)+2.3068016929e-02f, (faac_real)+2.4416099203e-02f, (faac_real)+2.5787584755e-02f, (faac_real)+2.7185942963e-02f,
    (faac_real)+2.8607217364e-02f, (faac_real)+3.0050265743e-02f, (faac_real)+3.1501760874e-02f, (faac_real)+3.2975408103e-02f,
    (faac_real)+3.4462094877e-02f, (faac_real)+3.5969756055e-02f, (faac_real)+3.7481285043e-02f, (faac_real)+3.9005367947e-02f,
    (faac_real)+4.0534917056e-02f, (faac_real)+4.2064909464e-02f, (faac_real)+4.3609754213e-02f, (faac_real)+4.5148840564e-02f,
    (faac_real)+4.6684302726e-02f, (faac_real)+4.8216572007e-02f, (faac_real)+4.9738575560e-02f, (faac_real)+5.1255615552e-02f,
    (faac_real)+5.2763074652e-02f, (faac_real)+5.4245276836e-02f, (faac_real)+5.5717364821e-02f, (faac_real)+5.7161645013e-02f,
    (faac_real)+5.8591568363e-02f, (faac_real)+5.9983748018e-02f, (faac_real)+6.1345517172e-02f, (faac_real)+6.2685780812e-02f,
    (faac_real)+6.3971589807e-02f, (faac_real)+6.5224710644e-02f, (faac_real)+6.6436751221e-02f, (faac_real)+6.7607598512e-02f,
    (faac_real)+6.8704382835e-02f, (faac_real)+6.9763024471e-02f, (faac_real)+7.0762871073e-02f, (faac_real)+7.1700267311e-02f,
    (faac_real)+7.2568258331e-02f, (faac_real)+7.3362025508e-02f, (faac_real)+7.4100364243e-02f, (faac_real)+7.4745255812e-02f,
    (faac_real)+7.5313733620e-02f, (faac_real)+7.5800835866e-02f, (faac_real)+7.6199247934e-02f, (faac_real)+7.6499217041e-02f,
    (faac_real)+7.6709349042e-02f, (faac_real)+7.6817397570e-02f, (faac_real)+7.6823001139e-02f, (faac_real)+7.6720492417e-02f,
    (faac_real)+7.6505071832e-02f, (faac_real)+7.6174832185e-02f, (faac_real)+7.5730575651e-02f, (faac_real)+7.5157625529e-02f,
    (faac_real)+7.4466439476e-02f, (faac_real)+7.3640600576e-02f, (faac_real)+7.2677464273e-02f, (faac_real)+7.1582636479e-02f,
    (faac_real)+7.0353307351e-02f, (faac_real)+6.8966401320e-02f, (faac_real)+6.7452502152e-02f, (faac_real)+6.5769066865e-02f,
    (faac_real)+6.3944480596e-02f, (faac_real)+6.1960277904e-02f, (faac_real)+5.9816657081e-02f, (faac_real)+5.7515269199e-02f,
    (faac_real)+5.5046003430e-02f, (faac_real)+5.2409382174e-02f, (faac_real)+4.9597867634e-02f, (faac_real)+4.6630330517e-02f,
    (faac_real)+4.3476878220e-02f, (faac_real)+4.0145827841e-02f, (faac_real)+3.6641811681e-02f, (faac_real)+3.2958393067e-02f,
    (faac_real)+2.9082400601e-02f, (faac_real)+2.5030756189e-02f, (faac_real)+2.0799707286e-02f, (faac_real)+1.6370125822e-02f,
    (faac_real)+1.1762383279e-02f, (faac_real)+6.9636862162e-03f, (faac_real)+1.9765601450e-03f, (faac_real)-3.2086896830e-03f,
    (faac_real)-8.5711749137e-03f, (faac_real)-1.4128882736e-02f, (faac_real)-1.9883412926e-02f, (faac_real)-2.5822728881e-02f,
    (faac_real)-3.1953127453e-02f, (faac_real)-3.8277657208e-02f, (faac_real)-4.4780682159e-02f, (faac_real)-5.1480417679e-02f,
    (faac_real)-5.8370532683e-02f, (faac_real)-6.5440985314e-02f, (faac_real)-7.2694330081e-02f, (faac_real)-8.0137293443e-02f,
    (faac_real)-8.7754753656e-02f, (faac_real)-9.5553335289e-02f, (faac_real)-1.0353295311e-01f, (faac_real)-1.1168269318e-01f,
    (faac_real)-1.2000779847e-01f, (faac_real)-1.2850028504e-01f, (faac_real)-1.3715517612e-01f, (faac_real)-1.4597664912e-01f,
    (faac_real)-1.5496070711e-01f, (faac_real)-1.6409588557e-01f, (faac_real)-1.7338081722e-01f, (faac_real)-1.8281725485e-01f,
    (faac_real)-1.9239667457e-01f, (faac_real)-2.0212501768e-01f, (faac_real)-2.1197358538e-01f, (faac_real)-2.2196526964e-01f,
    (faac_real)-2.3206908707e-01f, (faac_real)-2.4230168846e-01f, (faac_real)-2.5264803096e-01f, (faac_real)-2.6310532995e-01f,
    (faac_real)-2.7366340406e-01f, (faac_real)-2.8432141891e-01f, (faac_real)-2.9507167171e-01f, (faac_real)-3.0590985752e-01f,
    (faac_real)-3.1682789136e-01f, (faac_real)-3.2781137272e-01f, (faac_real)-3.3887226939e-01f, (faac_real)-3.4999141229e-01f,
    (faac_real)+3.6115899031e-01f, (faac_real)+3.7237955463e-01f, (faac_real)+3.8363500139e-01f, (faac_real)+3.9492117616e-01f,
    (faac_real)+4.0623176768e-01f, (faac_real)+4.1756968968e-01f, (faac_real)+4.2891199207e-01f, (faac_real)+4.4025537544e-01f,
    (faac_real)+4.5159965357e-01f, (faac_real)+4.6293080853e-01f, (faac_real)+4.7424532146e-01f, (faac_real)+4.8552530911e-01f,
    (faac_real)+4.9677082546e-01f, (faac_real)+5.0798175000e-01f, (faac_real)+5.1912349702e-01f, (faac_real)+5.3022408957e-01f,
    (faac_real)+5.4125534487e-01f, (faac_real)+5.5220512585e-01f, (faac_real)+5.6307891401e-01f, (faac_real)+5.7385241317e-01f,
    (faac_real)+5.8454032355e-01f, (faac_real)+5.9511230862e-01f, (faac_real)+6.0557835389e-01f, (faac_real)+6.1591099320e-01f,
    (faac_real)+6.2612426956e-01f, (faac_real)+6.3619801077e-01f, (faac_real)+6.4612696959e-01f, (faac_real)+6.5590163025e-01f,
    (faac_real)+6.6551398802e-01f, (faac_real)+6.7496631902e-01f, (faac_real)+6.8423532935e-01f, (faac_real)+6.9332823767e-01f,
    (faac_real)+7.0223887194e-01f, (faac_real)+7.1094104263e-01f, (faac_real)+7.1944626350e-01f, (faac_real)+7.2774489003e-01f,
    (faac_real)+7.3582117583e-01f, (faac_real)+7.4368278636e-01f, (faac_real)+7.5131374561e-01f, (faac_real)+7.5870807608e-01f,
    (faac_real)+7.6586748651e-01f, (faac_real)+7.7277808813e-01f, (faac_real)+7.7942875190e-01f, (faac_real)+7.8583531204e-01f,
    (faac_real)+7.9197358416e-01f, (faac_real)+7.9784664138e-01f, (faac_real)+8.0344857519e-01f, (faac_real)+8.0876950044e-01f,
    (faac_real)+8.1381912706e-01f, (faac_real)+8.1857760046e-01f, (faac_real)+8.2304198905e-01f, (faac_real)+8.2722753473e-01f,
    (faac_real)+8.3110384572e-01f, (faac_real)+8.3469373618e-01f, (faac_real)+8.3797173379e-01f, (faac_real)+8.4095413925e-01f,
    (faac_real)+8.4362382812e-01f, (faac_real)+8.4598184698e-01f, (faac_real)+8.4803157771e-01f, (faac_real)+8.4978051984e-01f,
    (faac_real)+8.5119715249e-01f, (faac_real)+8.5230470352e-01f, (faac_real)+8.5310209497e-01f, (faac_real)+8.5357205739e-01f,
    (faac_real)+8.5373856006e-01f
};

/* ------------------------------------------------------------------
 * SBR start-frequency offset table
 * ISO 14496-3:2009 Table 4.87  (bs_start_freq → k0 offset from start_min)
 * ISO 14496-3:2009 Table 4.87.
 *
 * Row selection by full SBR sample rate:
 *   row 0 : Fs_sbr  = 16000 Hz
 *   row 1 : Fs_sbr  = 22050 Hz
 *   row 2 : Fs_sbr  = 24000 Hz
 *   row 3 : Fs_sbr  = 32000 Hz
 *   row 4 : 44100 ≤ Fs_sbr ≤ 64000 Hz
 *   row 5 : Fs_sbr  > 64000 Hz
 *   row 6 : Single-rate SBR (bs_samplerate_mode = 0); used when SBR and core
 *           run at the same rate. FAAC only supports standard dual-rate HE-AAC.
 * ------------------------------------------------------------------ */
static const signed char sbr_offset[7][16] = {
    /* 16000   */ {-8,-7,-6,-5,-4,-3,-2,-1, 0, 1, 2, 3, 4, 5, 6, 7},
    /* 22050   */ {-5,-4,-3,-2,-1, 0, 1, 2, 3, 4, 5, 6, 7, 9,11,13},
    /* 24000   */ {-5,-3,-2,-1, 0, 1, 2, 3, 4, 5, 6, 7, 9,11,13,16},
    /* 32000   */ {-6,-4,-2,-1, 0, 1, 2, 3, 4, 5, 6, 7, 9,11,13,16},
    /* 44-64k  */ {-4,-2,-1, 0, 1, 2, 3, 4, 5, 6, 7, 9,11,13,16,20},
    /* >64k    */ {-2,-1, 0, 1, 2, 3, 4, 5, 6, 7, 9,11,13,16,20,24},
        /* Mode 0  */ { 0, 1, 2, 3, 4, 5, 6, 7, 9,11,13,16,20,24,28,33}, /* Single-rate SBR (bs_samplerate_mode=0); not used in FAAC dual-rate HE-AAC */
};

/* ------------------------------------------------------------------
 * Number of SBR frequency bands per octave (for master frequency
 * band table generation). Used with bs_num_bands (inside header).
 * We hardcode 10 bands/octave – a common midpoint value.
 * ------------------------------------------------------------------ */
#define SBR_NUM_BANDS_PER_OCTAVE 10

/* ------------------------------------------------------------------
 * SBR Huffman encoder tables
 * ISO 14496-3:2009 Huffman decode trees.
 *
 * Format: { code, len }
 *   code : codeword value, MSB at bit (len-1)  — pass directly to PutBit()
 *   len  : codeword length in bits
 *
 * Two tables are defined:
 *   f_huff_env_1_5dB : freq-domain envelope, 1.5 dB resolution (amp_res=0)
 *                      Used for non-coupling channels.
 *                      FIXFIX + 1 envelope forces amp_res=0 per ISO spec.
 *   f_huff_env_3_0dB : freq-domain envelope, 3.0 dB resolution (amp_res=1)
 *                      Also used for freq-domain noise floor coding.
 * ------------------------------------------------------------------ */

/* Encoder entry: { codeword, bit-length } */
typedef uint32_t SBRHuffEntry;

/*
 * f_huff_env_1_5dB — frequency-domain envelope, 1.5 dB steps, non-coupling
 * ISO 14496-3:2009 Table 4.85 (F_huffman_env_1_5dB)
 * Symbols: delta values -60..+60  (121 symbols)
 * Table index = delta + F_HUFF_ENV_1_5DB_OFFSET
 */
#define F_HUFF_ENV_1_5DB_OFFSET  60
#define F_HUFF_ENV_1_5DB_NSYMS   121
static const SBRHuffEntry f_huff_env_1_5dB[F_HUFF_ENV_1_5DB_NSYMS] = {
    /*  -60 */ 0x07ffe713u,
    /*  -59 */ 0x07ffe813u,
    /*  -58 */ 0x0fffd214u,
    /*  -57 */ 0x0fffd314u,
    /*  -56 */ 0x0fffd414u,
    /*  -55 */ 0x0fffd514u,
    /*  -54 */ 0x0fffd614u,
    /*  -53 */ 0x0fffd714u,
    /*  -52 */ 0x0fffd814u,
    /*  -51 */ 0x07ffda13u,
    /*  -50 */ 0x0fffd914u,
    /*  -49 */ 0x0fffda14u,
    /*  -48 */ 0x0fffdb14u,
    /*  -47 */ 0x0fffdc14u,
    /*  -46 */ 0x07ffdb13u,
    /*  -45 */ 0x0fffdd14u,
    /*  -44 */ 0x07ffdc13u,
    /*  -43 */ 0x07ffdd13u,
    /*  -42 */ 0x0fffde14u,
    /*  -41 */ 0x03ffe412u,
    /*  -40 */ 0x0fffdf14u,
    /*  -39 */ 0x0fffe014u,
    /*  -38 */ 0x0fffe114u,
    /*  -37 */ 0x07ffde13u,
    /*  -36 */ 0x0fffe214u,
    /*  -35 */ 0x0fffe314u,
    /*  -34 */ 0x0fffe414u,
    /*  -33 */ 0x07ffdf13u,
    /*  -32 */ 0x0fffe514u,
    /*  -31 */ 0x07ffe013u,
    /*  -30 */ 0x03ffe812u,
    /*  -29 */ 0x07ffe113u,
    /*  -28 */ 0x03ffe012u,
    /*  -27 */ 0x03ffe912u,
    /*  -26 */ 0x01ffef11u,
    /*  -25 */ 0x03ffe512u,
    /*  -24 */ 0x01ffec11u,
    /*  -23 */ 0x01ffed11u,
    /*  -22 */ 0x01ffee11u,
    /*  -21 */ 0x00fff410u,
    /*  -20 */ 0x00fff310u,
    /*  -19 */ 0x00fff010u,
    /*  -18 */ 0x007ff70fu,
    /*  -17 */ 0x007ff60fu,
    /*  -16 */ 0x003ffa0eu,
    /*  -15 */ 0x001ffa0du,
    /*  -14 */ 0x001ff90du,
    /*  -13 */ 0x000ffa0cu,
    /*  -12 */ 0x000ff80cu,
    /*  -11 */ 0x0007f90bu,
    /*  -10 */ 0x0003fb0au,
    /*   -9 */ 0x0001fc09u,
    /*   -8 */ 0x0001fa09u,
    /*   -7 */ 0x0000fb08u,
    /*   -6 */ 0x00007c07u,
    /*   -5 */ 0x00003c06u,
    /*   -4 */ 0x00001c05u,
    /*   -3 */ 0x00000c04u,
    /*   -2 */ 0x00000503u,
    /*   -1 */ 0x00000102u,
    /*    0 */ 0x00000002u,
    /*   +1 */ 0x00000403u,
    /*   +2 */ 0x00000d04u,
    /*   +3 */ 0x00001d05u,
    /*   +4 */ 0x00003d06u,
    /*   +5 */ 0x0000fa08u,
    /*   +6 */ 0x0000fc08u,
    /*   +7 */ 0x0001fb09u,
    /*   +8 */ 0x0003fa0au,
    /*   +9 */ 0x0007f80bu,
    /*  +10 */ 0x0007fa0bu,
    /*  +11 */ 0x0007fb0bu,
    /*  +12 */ 0x000ff90cu,
    /*  +13 */ 0x000ffb0cu,
    /*  +14 */ 0x001ff80du,
    /*  +15 */ 0x001ffb0du,
    /*  +16 */ 0x003ff80eu,
    /*  +17 */ 0x003ff90eu,
    /*  +18 */ 0x00fff110u,
    /*  +19 */ 0x00fff210u,
    /*  +20 */ 0x01ffea11u,
    /*  +21 */ 0x01ffeb11u,
    /*  +22 */ 0x03ffe112u,
    /*  +23 */ 0x03ffe212u,
    /*  +24 */ 0x03ffea12u,
    /*  +25 */ 0x03ffe312u,
    /*  +26 */ 0x03ffe612u,
    /*  +27 */ 0x03ffe712u,
    /*  +28 */ 0x03ffeb12u,
    /*  +29 */ 0x0fffe614u,
    /*  +30 */ 0x07ffe213u,
    /*  +31 */ 0x0fffe714u,
    /*  +32 */ 0x0fffe814u,
    /*  +33 */ 0x0fffe914u,
    /*  +34 */ 0x0fffea14u,
    /*  +35 */ 0x0fffeb14u,
    /*  +36 */ 0x0fffec14u,
    /*  +37 */ 0x07ffe313u,
    /*  +38 */ 0x0fffed14u,
    /*  +39 */ 0x0fffee14u,
    /*  +40 */ 0x0fffef14u,
    /*  +41 */ 0x0ffff014u,
    /*  +42 */ 0x07ffe413u,
    /*  +43 */ 0x0ffff114u,
    /*  +44 */ 0x03ffec12u,
    /*  +45 */ 0x0ffff214u,
    /*  +46 */ 0x0ffff314u,
    /*  +47 */ 0x07ffe513u,
    /*  +48 */ 0x07ffe613u,
    /*  +49 */ 0x0ffff414u,
    /*  +50 */ 0x0ffff514u,
    /*  +51 */ 0x0ffff614u,
    /*  +52 */ 0x0ffff714u,
    /*  +53 */ 0x0ffff814u,
    /*  +54 */ 0x0ffff914u,
    /*  +55 */ 0x0ffffa14u,
    /*  +56 */ 0x0ffffb14u,
    /*  +57 */ 0x0ffffc14u,
    /*  +58 */ 0x0ffffd14u,
    /*  +59 */ 0x0ffffe14u,
    /*  +60 */ 0x0fffff14u,
};

/*
 * f_huff_env_3_0dB — frequency-domain envelope, 3.0 dB steps, non-coupling
 * ISO 14496-3:2009 Table 4.87 (F_huffman_env_3_0dB)
 * Also used as the freq-domain noise-floor Huffman table (per ISO spec).
 * Symbols: delta values -31..+31  (63 symbols)
 * Table index = delta + F_HUFF_ENV_3_0DB_OFFSET
 */
#define F_HUFF_ENV_3_0DB_OFFSET  31
#define F_HUFF_ENV_3_0DB_NSYMS   63
static const SBRHuffEntry f_huff_env_3_0dB[F_HUFF_ENV_3_0DB_NSYMS] = {
    /*  -31 */ 0x0ffff014u,
    /*  -30 */ 0x0ffff114u,
    /*  -29 */ 0x0ffff214u,
    /*  -28 */ 0x0ffff314u,
    /*  -27 */ 0x0ffff414u,
    /*  -26 */ 0x0ffff514u,
    /*  -25 */ 0x0ffff614u,
    /*  -24 */ 0x03fff312u,
    /*  -23 */ 0x07fff513u,
    /*  -22 */ 0x07ffee13u,
    /*  -21 */ 0x07ffef13u,
    /*  -20 */ 0x07fff613u,
    /*  -19 */ 0x03fff412u,
    /*  -18 */ 0x03fff212u,
    /*  -17 */ 0x0ffff714u,
    /*  -16 */ 0x07fff013u,
    /*  -15 */ 0x01fff511u,
    /*  -14 */ 0x03fff012u,
    /*  -13 */ 0x01fff411u,
    /*  -12 */ 0x00fff710u,
    /*  -11 */ 0x00fff610u,
    /*  -10 */ 0x007ff80fu,
    /*   -9 */ 0x003ffb0eu,
    /*   -8 */ 0x000ffd0cu,
    /*   -7 */ 0x0007fd0bu,
    /*   -6 */ 0x0003fd0au,
    /*   -5 */ 0x0001fd09u,
    /*   -4 */ 0x0000fd08u,
    /*   -3 */ 0x00003e06u,
    /*   -2 */ 0x00000e04u,
    /*   -1 */ 0x00000202u,
    /*    0 */ 0x00000001u,
    /*   +1 */ 0x00000603u,
    /*   +2 */ 0x00001e05u,
    /*   +3 */ 0x0000fc08u,
    /*   +4 */ 0x0001fc09u,
    /*   +5 */ 0x0003fc0au,
    /*   +6 */ 0x0007fc0bu,
    /*   +7 */ 0x000ffc0cu,
    /*   +8 */ 0x001ffc0du,
    /*   +9 */ 0x003ffa0eu,
    /*  +10 */ 0x007ff90fu,
    /*  +11 */ 0x007ffa0fu,
    /*  +12 */ 0x00fff810u,
    /*  +13 */ 0x00fff910u,
    /*  +14 */ 0x01fff611u,
    /*  +15 */ 0x01fff711u,
    /*  +16 */ 0x03fff512u,
    /*  +17 */ 0x03fff612u,
    /*  +18 */ 0x03fff112u,
    /*  +19 */ 0x0ffff814u,
    /*  +20 */ 0x07fff113u,
    /*  +21 */ 0x07fff213u,
    /*  +22 */ 0x07fff313u,
    /*  +23 */ 0x0ffff914u,
    /*  +24 */ 0x07fff713u,
    /*  +25 */ 0x07fff413u,
    /*  +26 */ 0x0ffffa14u,
    /*  +27 */ 0x0ffffb14u,
    /*  +28 */ 0x0ffffc14u,
    /*  +29 */ 0x0ffffd14u,
    /*  +30 */ 0x0ffffe14u,
    /*  +31 */ 0x0fffff14u,
};

#ifdef __cplusplus
}
#endif

#endif /* SBR_TABLES_H */
