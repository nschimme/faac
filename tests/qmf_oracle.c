#include <math.h>
#include <string.h>
#include "qmf_oracle.h"
#include "libfaac/faac_real.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const faac_real sbr_qmf_window_ds[64] = {
     (faac_real)0.00000000e+00, (faac_real)5.36548976e-04, (faac_real)1.49098137e-03, (faac_real)3.14336202e-03,
     (faac_real)5.27748948e-03, (faac_real)7.68975685e-03, (faac_real)1.01109461e-02, (faac_real)1.20138462e-02,
     (faac_real)1.28756718e-02, (faac_real)1.20264939e-02, (faac_real)8.97602006e-03, (faac_real)2.51825292e-03,
    (faac_real)-7.29687483e-03, (faac_real)-2.08397195e-02, (faac_real)-3.62228565e-02, (faac_real)-5.23067017e-02,
    (faac_real)-6.69739674e-02, (faac_real)-7.82829581e-02, (faac_real)-8.40448915e-02, (faac_real)-8.21108721e-02,
    (faac_real)-7.07856267e-02, (faac_real)-4.88382545e-02, (faac_real)-1.65773682e-02, (faac_real)2.50142761e-02,
     (faac_real)7.14696624e-02, (faac_real)1.24418671e-01, (faac_real)1.80857862e-01, (faac_real)2.39440231e-01,
     (faac_real)2.98457860e-01, (faac_real)3.56453777e-01, (faac_real)4.12082039e-01, (faac_real)4.64118950e-01,
     (faac_real)4.64118950e-01, (faac_real)4.12082039e-01, (faac_real)3.56453777e-01, (faac_real)2.98457860e-01,
     (faac_real)2.39440231e-01, (faac_real)1.80857862e-01, (faac_real)1.24418671e-01, (faac_real)7.14696624e-02,
     (faac_real)2.50142761e-02, (faac_real)-1.65773682e-02, (faac_real)-4.88382545e-02, (faac_real)-7.07856267e-02,
    (faac_real)-8.21108721e-02, (faac_real)-8.40448915e-02, (faac_real)-7.82829581e-02, (faac_real)-6.69739674e-02,
    (faac_real)-5.23067017e-02, (faac_real)-3.62228565e-02, (faac_real)-2.08397195e-02, (faac_real)-7.29687483e-03,
     (faac_real)2.51825292e-03, (faac_real)8.97602006e-03, (faac_real)1.20264939e-02, (faac_real)1.28756718e-02,
     (faac_real)1.20138462e-02, (faac_real)1.01109461e-02, (faac_real)7.68975685e-03, (faac_real)5.27748948e-03,
     (faac_real)3.14336202e-03, (faac_real)1.49098137e-03, (faac_real)5.36548976e-04, (faac_real)0.00000000e+00
};

void qmf_ref_slot(const faac_real input[32], faac_real state[320], faac_real W_re[32], faac_real W_im[32]) {
    memmove(state, state + 32, (64-32)*sizeof(faac_real));
    memcpy(state + 32, input, 32 * sizeof(faac_real));
    for (int k = 0; k < 32; k++) {
        faac_real re = (faac_real)0.0, im = (faac_real)0.0;
        faac_real phase_step = (faac_real)M_PI * (faac_real)(2 * k + 1) / (faac_real)128.0;
        for (int n = 0; n < 64; n++) {
            faac_real hv = sbr_qmf_window_ds[n] * state[n];
            faac_real phase = phase_step * (faac_real)(2 * n - 63);
            re += hv * FAAC_COS(phase);
            im += hv * FAAC_SIN(phase);
        }
        W_re[k] = re;
        W_im[k] = im;
    }
}

static const faac_real sbr_qmf_window_us640_ref[640] = {
    (faac_real)+0.0000000000e+00, (faac_real)-5.5252860000e-04, (faac_real)-5.6176920000e-04, (faac_real)-4.9475180000e-04,
    (faac_real)-4.8752270000e-04, (faac_real)-4.8937910000e-04, (faac_real)-5.0407140000e-04, (faac_real)-5.2265640000e-04,
    (faac_real)-5.4665650000e-04, (faac_real)-5.6778020000e-04, (faac_real)-5.8709300000e-04, (faac_real)-6.1327470000e-04,
    (faac_real)-6.3124930000e-04, (faac_real)-6.5403330000e-04, (faac_real)-6.7776900000e-04, (faac_real)-6.9416140000e-04,
    (faac_real)-7.1577360000e-04, (faac_real)-7.2550430000e-04, (faac_real)-7.4409410000e-04, (faac_real)-7.4905980000e-04,
    (faac_real)-7.6813710000e-04, (faac_real)-7.7248480000e-04, (faac_real)-7.8343320000e-04, (faac_real)-7.7798690000e-04,
    (faac_real)-7.8036640000e-04, (faac_real)-7.8014490000e-04, (faac_real)-7.7579770000e-04, (faac_real)-7.6307930000e-04,
    (faac_real)-7.5300010000e-04, (faac_real)-7.3193570000e-04, (faac_real)-7.2153910000e-04, (faac_real)-6.9179370000e-04,
    (faac_real)-6.6504150000e-04, (faac_real)-6.3415940000e-04, (faac_real)-5.9461180000e-04, (faac_real)-5.5645760000e-04,
    (faac_real)-5.1455720000e-04, (faac_real)-4.6063250000e-04, (faac_real)-4.0951210000e-04, (faac_real)-3.5011750000e-04,
    (faac_real)-2.8969810000e-04, (faac_real)-2.0983370000e-04, (faac_real)-1.4463800000e-04, (faac_real)-6.1733400000e-05,
    (faac_real)+1.3494900000e-05, (faac_real)+1.0943830000e-04, (faac_real)+2.0430170000e-04, (faac_real)+2.9495310000e-04,
    (faac_real)+4.0265400000e-04, (faac_real)+5.1073880000e-04, (faac_real)+6.2393760000e-04, (faac_real)+7.4580250000e-04,
    (faac_real)+8.6084430000e-04, (faac_real)+9.8859880000e-04, (faac_real)+1.1250155000e-03, (faac_real)+1.2577884000e-03,
    (faac_real)+1.3902494000e-03, (faac_real)+1.5443219000e-03, (faac_real)+1.6868083000e-03, (faac_real)+1.8348265000e-03,
    (faac_real)+1.9841140000e-03, (faac_real)+2.1461583000e-03, (faac_real)+2.3017254000e-03, (faac_real)+2.4625616000e-03,
    (faac_real)+2.6201758000e-03, (faac_real)+2.7870464000e-03, (faac_real)+2.9469447000e-03, (faac_real)+3.1125420000e-03,
    (faac_real)+3.2739613000e-03, (faac_real)+3.4418874000e-03, (faac_real)+3.6008268000e-03, (faac_real)+3.7603922000e-03,
    (faac_real)+3.9207432000e-03, (faac_real)+4.0819753000e-03, (faac_real)+4.2264269000e-03, (faac_real)+4.3730719000e-03,
    (faac_real)+4.5209852000e-03, (faac_real)+4.6606460000e-03, (faac_real)+4.7932560000e-03, (faac_real)+4.9137603000e-03,
    (faac_real)+5.0393022000e-03, (faac_real)+5.1407353000e-03, (faac_real)+5.2461166000e-03, (faac_real)+5.3471681000e-03,
    (faac_real)+5.4196775000e-03, (faac_real)+5.4876040000e-03, (faac_real)+5.5475714000e-03, (faac_real)+5.5938023000e-03,
    (faac_real)+5.6220643000e-03, (faac_real)+5.6455196000e-03, (faac_real)+5.6389199000e-03, (faac_real)+5.6266114000e-03,
    (faac_real)+5.5917128000e-03, (faac_real)+5.5404363000e-03, (faac_real)+5.4753783000e-03, (faac_real)+5.3838975000e-03,
    (faac_real)+5.2715758000e-03, (faac_real)+5.1382275000e-03, (faac_real)+4.9839687000e-03, (faac_real)+4.8109469000e-03,
    (faac_real)+4.6039530000e-03, (faac_real)+4.3801861000e-03, (faac_real)+4.1251642000e-03, (faac_real)+3.8456408000e-03,
    (faac_real)+3.5401246000e-03, (faac_real)+3.2091885000e-03, (faac_real)+2.8446757000e-03, (faac_real)+2.4508540000e-03,
    (faac_real)+2.0274176000e-03, (faac_real)+1.5784682000e-03, (faac_real)+1.0902329000e-03, (faac_real)+5.8322640000e-04,
    (faac_real)+2.7604500000e-05, (faac_real)-5.4642800000e-04, (faac_real)-1.1568135000e-03, (faac_real)-1.8039472000e-03,
    (faac_real)-2.4826723000e-03, (faac_real)-3.1933778000e-03, (faac_real)-3.9401124000e-03, (faac_real)-4.7222596000e-03,
    (faac_real)-5.5337211000e-03, (faac_real)-6.3792293000e-03, (faac_real)-7.2615816000e-03, (faac_real)-8.1798233000e-03,
    (faac_real)-9.1325329000e-03, (faac_real)-1.0115021500e-02, (faac_real)-1.1131554800e-02, (faac_real)-1.2184999500e-02,
    (faac_real)+1.3271822000e-02, (faac_real)+1.4390466600e-02, (faac_real)+1.5540555300e-02, (faac_real)+1.6732471200e-02,
    (faac_real)+1.7943338100e-02, (faac_real)+1.9187243100e-02, (faac_real)+2.0453179300e-02, (faac_real)+2.1746755000e-02,
    (faac_real)+2.3068016900e-02, (faac_real)+2.4416099200e-02, (faac_real)+2.5787584700e-02, (faac_real)+2.7185942900e-02,
    (faac_real)+2.8607217300e-02, (faac_real)+3.0050265700e-02, (faac_real)+3.1501760800e-02, (faac_real)+3.2975408100e-02,
    (faac_real)+3.4462094800e-02, (faac_real)+3.5969756000e-02, (faac_real)+3.7481285000e-02, (faac_real)+3.9005367900e-02,
    (faac_real)+4.0534917000e-02, (faac_real)+4.2064909400e-02, (faac_real)+4.3609754200e-02, (faac_real)+4.5148840500e-02,
    (faac_real)+4.6684302700e-02, (faac_real)+4.8216572000e-02, (faac_real)+4.9738575500e-02, (faac_real)+5.1255615500e-02,
    (faac_real)+5.2763074600e-02, (faac_real)+5.4245276800e-02, (faac_real)+5.5717364800e-02, (faac_real)+5.7161645000e-02,
    (faac_real)+5.8591568300e-02, (faac_real)+5.9983748000e-02, (faac_real)+6.1345517100e-02, (faac_real)+6.2685780800e-02,
    (faac_real)+6.3971589800e-02, (faac_real)+6.5224710600e-02, (faac_real)+6.6436751200e-02, (faac_real)+6.7607598500e-02,
    (faac_real)+6.8704382800e-02, (faac_real)+6.9763024400e-02, (faac_real)+7.0762871000e-02, (faac_real)+7.1700267300e-02,
    (faac_real)+7.2568258300e-02, (faac_real)+7.3362025500e-02, (faac_real)+7.4100364200e-02, (faac_real)+7.4745255800e-02,
    (faac_real)+7.5313733600e-02, (faac_real)+7.5800835800e-02, (faac_real)+7.6199247900e-02, (faac_real)+7.6499217000e-02,
    (faac_real)+7.6709349000e-02, (faac_real)+7.6817397500e-02, (faac_real)+7.6823001100e-02, (faac_real)+7.6720492400e-02,
    (faac_real)+7.6505071800e-02, (faac_real)+7.6174832100e-02, (faac_real)+7.5730575600e-02, (faac_real)+7.5157625500e-02,
    (faac_real)+7.4466439400e-02, (faac_real)+7.3640600500e-02, (faac_real)+7.2677464200e-02, (faac_real)+7.1582636400e-02,
    (faac_real)+7.0353307300e-02, (faac_real)+6.8966401300e-02, (faac_real)+6.7452502100e-02, (faac_real)+6.5769066800e-02,
    (faac_real)+6.3944480500e-02, (faac_real)+6.1960277900e-02, (faac_real)+5.9816657000e-02, (faac_real)+5.7515269100e-02,
    (faac_real)+5.5046003400e-02, (faac_real)+5.2409382100e-02, (faac_real)+4.9597867600e-02, (faac_real)+4.6630330500e-02,
    (faac_real)+4.3476878200e-02, (faac_real)+4.0145827800e-02, (faac_real)+3.6641811600e-02, (faac_real)+3.2958393000e-02,
    (faac_real)+2.9082400600e-02, (faac_real)+2.5030756100e-02, (faac_real)+2.0799707200e-02, (faac_real)+1.6370125800e-02,
    (faac_real)+1.1762383200e-02, (faac_real)+6.9636862000e-03, (faac_real)+1.9765601000e-03, (faac_real)-3.2086896000e-03,
    (faac_real)-8.5711749000e-03, (faac_real)-1.4128882700e-02, (faac_real)-1.9883412900e-02, (faac_real)-2.5822728800e-02,
    (faac_real)-3.1953127400e-02, (faac_real)-3.8277657200e-02, (faac_real)-4.4780682100e-02, (faac_real)-5.1480417600e-02,
    (faac_real)-5.8370532600e-02, (faac_real)-6.5440985300e-02, (faac_real)-7.2694330000e-02, (faac_real)-8.0137293400e-02,
    (faac_real)-8.7754753600e-02, (faac_real)-9.5553335200e-02, (faac_real)-1.0353295310e-01, (faac_real)-1.1168269310e-01,
    (faac_real)-1.2000779840e-01, (faac_real)-1.2850028500e-01, (faac_real)-1.3715517610e-01, (faac_real)-1.4597664910e-01,
    (faac_real)-1.5496070710e-01, (faac_real)-1.6409588550e-01, (faac_real)-1.7338081720e-01, (faac_real)-1.8281725480e-01,
    (faac_real)-1.9239667450e-01, (faac_real)-2.0212501760e-01, (faac_real)-2.1197358530e-01, (faac_real)-2.2196526960e-01,
    (faac_real)-2.3206908700e-01, (faac_real)-2.4230168840e-01, (faac_real)-2.5264803090e-01, (faac_real)-2.6310532990e-01,
    (faac_real)-2.7366340400e-01, (faac_real)-2.8432141890e-01, (faac_real)-2.9507167170e-01, (faac_real)-3.0590985750e-01,
    (faac_real)-3.1682789130e-01, (faac_real)-3.2781137270e-01, (faac_real)-3.3887226930e-01, (faac_real)-3.4999141220e-01,
    (faac_real)+3.6115899030e-01, (faac_real)+3.7237955460e-01, (faac_real)+3.8363500130e-01, (faac_real)+3.9492117610e-01,
    (faac_real)+4.0623176760e-01, (faac_real)+4.1756968960e-01, (faac_real)+4.2891199200e-01, (faac_real)+4.4025537540e-01,
    (faac_real)+4.5159965350e-01, (faac_real)+4.6293080850e-01, (faac_real)+4.7424532140e-01, (faac_real)+4.8552530910e-01,
    (faac_real)+4.9677082540e-01, (faac_real)+5.0798175000e-01, (faac_real)+5.1912349700e-01, (faac_real)+5.3022408950e-01,
    (faac_real)+5.4125534480e-01, (faac_real)+5.5220512580e-01, (faac_real)+5.6307891400e-01, (faac_real)+5.7385241310e-01,
    (faac_real)+5.8454032350e-01, (faac_real)+5.9511230860e-01, (faac_real)+6.0557835380e-01, (faac_real)+6.1591099320e-01,
    (faac_real)+6.2612426950e-01, (faac_real)+6.3619801070e-01, (faac_real)+6.4612696950e-01, (faac_real)+6.5590163020e-01,
    (faac_real)+6.6551398800e-01, (faac_real)+6.7496631900e-01, (faac_real)+6.8423532930e-01, (faac_real)+6.9332823760e-01,
    (faac_real)+7.0223887190e-01, (faac_real)+7.1094104260e-01, (faac_real)+7.1944626340e-01, (faac_real)+7.2774489000e-01,
    (faac_real)+7.3582117580e-01, (faac_real)+7.4368278630e-01, (faac_real)+7.5131374560e-01, (faac_real)+7.5870807600e-01,
    (faac_real)+7.6586748650e-01, (faac_real)+7.7277808810e-01, (faac_real)+7.7942875190e-01, (faac_real)+7.8583531200e-01,
    (faac_real)+7.9197358410e-01, (faac_real)+7.9784664130e-01, (faac_real)+8.0344857510e-01, (faac_real)+8.0876950040e-01,
    (faac_real)+8.1381912700e-01, (faac_real)+8.1857760040e-01, (faac_real)+8.2304198900e-01, (faac_real)+8.2722753470e-01,
    (faac_real)+8.3110384570e-01, (faac_real)+8.3469373610e-01, (faac_real)+8.3797173370e-01, (faac_real)+8.4095413920e-01,
    (faac_real)+8.4362382810e-01, (faac_real)+8.4598184690e-01, (faac_real)+8.4803157770e-01, (faac_real)+8.4978051980e-01,
    (faac_real)+8.5119715240e-01, (faac_real)+8.5230470350e-01, (faac_real)+8.5310209490e-01, (faac_real)+8.5357205730e-01,
    (faac_real)+8.5373856000e-01, (faac_real)+8.5357205730e-01, (faac_real)+8.5310209490e-01, (faac_real)+8.5230470350e-01,
    (faac_real)+8.5119715240e-01, (faac_real)+8.4978051980e-01, (faac_real)+8.4803157770e-01, (faac_real)+8.4598184690e-01,
    (faac_real)+8.4362382810e-01, (faac_real)+8.4095413920e-01, (faac_real)+8.3797173370e-01, (faac_real)+8.3469373610e-01,
    (faac_real)+8.3110384570e-01, (faac_real)+8.2722753470e-01, (faac_real)+8.2304198900e-01, (faac_real)+8.1857760040e-01,
    (faac_real)+8.1381912700e-01, (faac_real)+8.0876950040e-01, (faac_real)+8.0344857510e-01, (faac_real)+7.9784664130e-01,
    (faac_real)+7.9197358410e-01, (faac_real)+7.8583531200e-01, (faac_real)+7.7942875190e-01, (faac_real)+7.7277808810e-01,
    (faac_real)+7.6586748650e-01, (faac_real)+7.5870807600e-01, (faac_real)+7.5131374560e-01, (faac_real)+7.4368278630e-01,
    (faac_real)+7.3582117580e-01, (faac_real)+7.2774489000e-01, (faac_real)+7.1944626340e-01, (faac_real)+7.1094104260e-01,
    (faac_real)+7.0223887190e-01, (faac_real)+6.9332823760e-01, (faac_real)+6.8423532930e-01, (faac_real)+6.7496631900e-01,
    (faac_real)+6.6551398800e-01, (faac_real)+6.5590163020e-01, (faac_real)+6.4612696950e-01, (faac_real)+6.3619801070e-01,
    (faac_real)+6.2612426950e-01, (faac_real)+6.1591099320e-01, (faac_real)+6.0557835380e-01, (faac_real)+5.9511230860e-01,
    (faac_real)+5.8454032350e-01, (faac_real)+5.7385241310e-01, (faac_real)+5.6307891400e-01, (faac_real)+5.5220512580e-01,
    (faac_real)+5.4125534480e-01, (faac_real)+5.3022408950e-01, (faac_real)+5.1912349700e-01, (faac_real)+5.0798175000e-01,
    (faac_real)+4.9677082540e-01, (faac_real)+4.8552530910e-01, (faac_real)+4.7424532140e-01, (faac_real)+4.6293080850e-01,
    (faac_real)+4.5159965350e-01, (faac_real)+4.4025537540e-01, (faac_real)+4.2891199200e-01, (faac_real)+4.1756968960e-01,
    (faac_real)+4.0623176760e-01, (faac_real)+3.9492117610e-01, (faac_real)+3.8363500130e-01, (faac_real)+3.7237955460e-01,
    (faac_real)-3.6115899030e-01, (faac_real)-3.4999141220e-01, (faac_real)-3.3887226930e-01, (faac_real)-3.2781137270e-01,
    (faac_real)-3.1682789130e-01, (faac_real)-3.0590985750e-01, (faac_real)-2.9507167170e-01, (faac_real)-2.8432141890e-01,
    (faac_real)-2.7366340400e-01, (faac_real)-2.6310532990e-01, (faac_real)-2.5264803090e-01, (faac_real)-2.4230168840e-01,
    (faac_real)-2.3206908700e-01, (faac_real)-2.2196526960e-01, (faac_real)-2.1197358530e-01, (faac_real)-2.0212501760e-01,
    (faac_real)-1.9239667450e-01, (faac_real)-1.8281725480e-01, (faac_real)-1.7338081720e-01, (faac_real)-1.6409588550e-01,
    (faac_real)-1.5496070710e-01, (faac_real)-1.4597664910e-01, (faac_real)-1.3715517610e-01, (faac_real)-1.2850028500e-01,
    (faac_real)-1.2000779840e-01, (faac_real)-1.1168269310e-01, (faac_real)-1.0353295310e-01, (faac_real)-9.5553335200e-02,
    (faac_real)-8.7754753600e-02, (faac_real)-8.0137293400e-02, (faac_real)-7.2694330000e-02, (faac_real)-6.5440985300e-02,
    (faac_real)-5.8370532600e-02, (faac_real)-5.1480417600e-02, (faac_real)-4.4780682100e-02, (faac_real)-3.8277657200e-02,
    (faac_real)-3.1953127400e-02, (faac_real)-2.5822728800e-02, (faac_real)-1.9883412900e-02, (faac_real)-1.4128882700e-02,
    (faac_real)-8.5711749000e-03, (faac_real)-3.2086896000e-03, (faac_real)+1.9765601000e-03, (faac_real)+6.9636862000e-03,
    (faac_real)+1.1762383200e-02, (faac_real)+1.6370125800e-02, (faac_real)+2.0799707200e-02, (faac_real)+2.5030756100e-02,
    (faac_real)+2.9082400600e-02, (faac_real)+3.2958393000e-02, (faac_real)+3.6641811600e-02, (faac_real)+4.0145827800e-02,
    (faac_real)+4.3476878200e-02, (faac_real)+4.6630330500e-02, (faac_real)+4.9597867600e-02, (faac_real)+5.2409382100e-02,
    (faac_real)+5.5046003400e-02, (faac_real)+5.7515269100e-02, (faac_real)+5.9816657000e-02, (faac_real)+6.1960277900e-02,
    (faac_real)+6.3944480500e-02, (faac_real)+6.5769066800e-02, (faac_real)+6.7452502100e-02, (faac_real)+6.8966401300e-02,
    (faac_real)+7.0353307300e-02, (faac_real)+7.1582636400e-02, (faac_real)+7.2677464200e-02, (faac_real)+7.3640600500e-02,
    (faac_real)+7.4466439400e-02, (faac_real)+7.5157625500e-02, (faac_real)+7.5730575600e-02, (faac_real)+7.6174832100e-02,
    (faac_real)+7.6505071800e-02, (faac_real)+7.6720492400e-02, (faac_real)+7.6817397500e-02, (faac_real)+7.6817397500e-02,
    (faac_real)+7.6709349000e-02, (faac_real)+7.6499217000e-02, (faac_real)+7.6199247900e-02, (faac_real)+7.5800835800e-02,
    (faac_real)+7.5313733600e-02, (faac_real)+7.4745255800e-02, (faac_real)+7.4100364200e-02, (faac_real)+7.3362025500e-02,
    (faac_real)+7.2568258300e-02, (faac_real)+7.1700267300e-02, (faac_real)+7.0762871000e-02, (faac_real)+6.9763024400e-02,
    (faac_real)+6.8704382800e-02, (faac_real)+6.7607598500e-02, (faac_real)+6.6436751200e-02, (faac_real)+6.5224710600e-02,
    (faac_real)+6.3971589800e-02, (faac_real)+6.2685780800e-02, (faac_real)+6.1345517100e-02, (faac_real)+5.9983748000e-02,
    (faac_real)+5.8591568300e-02, (faac_real)+5.7161645000e-02, (faac_real)+5.5717364800e-02, (faac_real)+5.4245276800e-02,
    (faac_real)+5.2763074600e-02, (faac_real)+5.1255615500e-02, (faac_real)+4.9738575500e-02, (faac_real)+4.8216572000e-02,
    (faac_real)+4.6684302700e-02, (faac_real)+4.5148840500e-02, (faac_real)+4.3609754200e-02, (faac_real)+4.2064909400e-02,
    (faac_real)+4.0534917000e-02, (faac_real)+3.9005367900e-02, (faac_real)+3.7481285000e-02, (faac_real)+3.5969756000e-02,
    (faac_real)+3.4462094800e-02, (faac_real)+3.2975408100e-02, (faac_real)+3.1501760800e-02, (faac_real)+3.0050265700e-02,
    (faac_real)+2.8607217300e-02, (faac_real)+2.7185942900e-02, (faac_real)+2.5787584700e-02, (faac_real)+2.4416099200e-02,
    (faac_real)+2.3068016900e-02, (faac_real)+2.1746755000e-02, (faac_real)+2.0453179300e-02, (faac_real)+1.9187243100e-02,
    (faac_real)+1.7943338100e-02, (faac_real)+1.6732471200e-02, (faac_real)+1.5540555300e-02, (faac_real)+1.4390466600e-02,
    (faac_real)-1.3271822000e-02, (faac_real)-1.2184999500e-02, (faac_real)-1.1131554800e-02, (faac_real)-1.0115021500e-02,
    (faac_real)-9.1325329000e-03, (faac_real)-8.1798233000e-03, (faac_real)-7.2615816000e-03, (faac_real)-6.3792293000e-03,
    (faac_real)-5.5337211000e-03, (faac_real)-4.7222596000e-03, (faac_real)-3.9401124000e-03, (faac_real)-3.1933778000e-03,
    (faac_real)-2.4826723000e-03, (faac_real)-1.8039472000e-03, (faac_real)-1.1568135000e-03, (faac_real)-5.4642800000e-04,
    (faac_real)+2.7604500000e-05, (faac_real)+5.8322640000e-04, (faac_real)+1.0902329000e-03, (faac_real)+1.5784682000e-03,
    (faac_real)+2.0274176000e-03, (faac_real)+2.4508540000e-03, (faac_real)+2.8446757000e-03, (faac_real)+3.2091885000e-03,
    (faac_real)+3.5401246000e-03, (faac_real)+3.8456408000e-03, (faac_real)+4.1251642000e-03, (faac_real)+4.3801861000e-03,
    (faac_real)+4.6039530000e-03, (faac_real)+4.8109469000e-03, (faac_real)+4.9839687000e-03, (faac_real)+5.1382275000e-03,
    (faac_real)+5.2715758000e-03, (faac_real)+5.3838975000e-03, (faac_real)+5.4753783000e-03, (faac_real)+5.5404363000e-03,
    (faac_real)+5.5917128000e-03, (faac_real)+5.6266114000e-03, (faac_real)+5.6389199000e-03, (faac_real)+5.6455196000e-03,
    (faac_real)+5.6220643000e-03, (faac_real)+5.5938023000e-03, (faac_real)+5.5475714000e-03, (faac_real)+5.4876040000e-03,
    (faac_real)+5.4196775000e-03, (faac_real)+5.3471681000e-03, (faac_real)+5.2461166000e-03, (faac_real)+5.1407353000e-03,
    (faac_real)+5.0393022000e-03, (faac_real)+4.9137603000e-03, (faac_real)+4.7932560000e-03, (faac_real)+4.6606460000e-03,
    (faac_real)+4.5209852000e-03, (faac_real)+4.3730719000e-03, (faac_real)+4.2264269000e-03, (faac_real)+4.0819753000e-03,
    (faac_real)+3.9207432000e-03, (faac_real)+3.7603922000e-03, (faac_real)+3.6008268000e-03, (faac_real)+3.4418874000e-03,
    (faac_real)+3.2739613000e-03, (faac_real)+3.1125420000e-03, (faac_real)+2.9469447000e-03, (faac_real)+2.7870464000e-03,
    (faac_real)+2.6201758000e-03, (faac_real)+2.4625616000e-03, (faac_real)+2.3017254000e-03, (faac_real)+2.1461583000e-03,
    (faac_real)+1.9841140000e-03, (faac_real)+1.8348265000e-03, (faac_real)+1.6868083000e-03, (faac_real)+1.5443219000e-03,
    (faac_real)+1.3902494000e-03, (faac_real)+1.2577884000e-03, (faac_real)+1.1250155000e-03, (faac_real)+9.8859880000e-04,
    (faac_real)+8.6084430000e-04, (faac_real)+7.4580250000e-04, (faac_real)+6.2393760000e-04, (faac_real)+5.1073880000e-04,
    (faac_real)+4.0265400000e-04, (faac_real)+2.9495310000e-04, (faac_real)+2.0430170000e-04, (faac_real)+1.0943830000e-04,
    (faac_real)+1.3494900000e-05, (faac_real)-6.1733400000e-05, (faac_real)-1.4463800000e-04, (faac_real)-2.0983370000e-04,
    (faac_real)-2.8969810000e-04, (faac_real)-3.5011750000e-04, (faac_real)-4.0951210000e-04, (faac_real)-4.6063250000e-04,
    (faac_real)-5.1455720000e-04, (faac_real)-5.5645760000e-04, (faac_real)-5.9461180000e-04, (faac_real)-6.3415940000e-04,
    (faac_real)-6.6504150000e-04, (faac_real)-6.9179370000e-04, (faac_real)-7.2153910000e-04, (faac_real)-7.3193570000e-04,
    (faac_real)-7.5300010000e-04, (faac_real)-7.6307930000e-04, (faac_real)-7.7579770000e-04, (faac_real)-7.8014490000e-04,
    (faac_real)-7.8036640000e-04, (faac_real)-7.7798690000e-04, (faac_real)-7.8343320000e-04, (faac_real)-7.7248480000e-04,
    (faac_real)-7.6813710000e-04, (faac_real)-7.4905980000e-04, (faac_real)-7.4409410000e-04, (faac_real)-7.2550430000e-04,
};

void qmf_ref_64_slot_energy(const faac_real input[64], faac_real state[640], faac_real energy[64]) {
    memmove(state, state + 64, 576 * sizeof(faac_real));
    memcpy(state + 576, input, 64 * sizeof(faac_real));
    faac_real u[128];
    for (int n = 0; n < 128; n++) {
        u[n] = sbr_qmf_window_us640_ref[n]       * state[639 - n]
             + sbr_qmf_window_us640_ref[n + 128] * state[511 - n]
             + sbr_qmf_window_us640_ref[n + 256] * state[383 - n]
             + sbr_qmf_window_us640_ref[n + 384] * state[255 - n]
             + sbr_qmf_window_us640_ref[n + 512] * state[127 - n];
    }
    for (int k = 0; k < 64; k++) {
        faac_real re = (faac_real)0.0, im = (faac_real)0.0;
        faac_real phase_step = (faac_real)M_PI * (faac_real)(2.0 * k + 1.0) / (faac_real)256.0;
        for (int n = 0; n < 128; n++) {
            faac_real phase = phase_step * (faac_real)(2 * n - 127);
            re += u[n] * FAAC_COS(phase);
            im += u[n] * FAAC_SIN(phase);
        }
        energy[k] = re * re + im * im;
    }
}
