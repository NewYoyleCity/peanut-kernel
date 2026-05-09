#ifndef HDMI_H
#define HDMI_H

#include "freelib/kstdint.h"

void hdmi_init(void);
int hdmi_present(void);
const char* hdmi_status(void);

#endif
