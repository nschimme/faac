#ifndef FAAC_CONFIG_H
#define FAAC_CONFIG_H

#ifndef PACKAGE
#define PACKAGE "faac"
#endif

#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "2.1.0"
#endif

#ifndef FAAC_SBR_DECIMATION
#define FAAC_SBR_DECIMATION 1
#endif

#ifndef MAX_CHANNELS
#if defined(ARDUINO) || defined(ESP32) || defined(ESP8266) || defined(__ARM_ARCH)
#define MAX_CHANNELS 2
#else
#define MAX_CHANNELS 6
#endif
#endif

#endif /* FAAC_CONFIG_H */
