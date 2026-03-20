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

#ifndef LINKAGE_H
#define LINKAGE_H

#if defined(__GNUC__) || defined(__clang__)
#define FAAC_HIDDEN __attribute__((visibility("hidden")))
#define FAAC_UNUSED __attribute__((unused))
#else
#define FAAC_HIDDEN
#define FAAC_UNUSED
#endif

#ifdef FAAC_TEST
/* Internal linkage for test visibility without public API exposure */
#define FAAC_PRIVATE FAAC_HIDDEN
#else
/* File-local scoping for maximum IPO and inlining performance */
#define FAAC_PRIVATE static FAAC_UNUSED
#endif

#endif /* LINKAGE_H */
