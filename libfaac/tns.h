#ifndef TNS_H
#define TNS_H

#include "faac_real.h"
#include "coder.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct faacEncStruct;

void TnsInit(struct faacEncStruct* hEncoder);
void TnsEnd(struct faacEncStruct* hEncoder);
void TnsEncode(struct faacEncStruct* hEncoder, TnsInfo* tnsInfo, int numberOfBands,int maxSfb,enum WINDOW_TYPE blockType,
               int* sfbOffsetTable,faac_real* spec);
void TnsEncodeFilterOnly(struct faacEncStruct* hEncoder, TnsInfo* tnsInfo, int numberOfBands, int maxSfb,
                         enum WINDOW_TYPE blockType, int *sfbOffsetTable, faac_real *spec);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* TNS_H */
