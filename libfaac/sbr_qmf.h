/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Jules
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
 *
 */

#ifndef SBR_QMF_H
#define SBR_QMF_H

#include "faac_real.h"

typedef struct {
    faac_real x[640]; /* sliding state buffer */
    int x_idx;
} sbr_qmf_analysis_t;

void sbr_qmf_analysis_init(sbr_qmf_analysis_t *qmf);
void sbr_qmf_analysis(sbr_qmf_analysis_t *qmf, const faac_real *input, faac_real X[32][64][2]);

#endif /* SBR_QMF_H */
