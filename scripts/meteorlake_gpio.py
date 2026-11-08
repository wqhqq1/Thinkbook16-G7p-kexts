"""Prepare the Meteor Lake-P pin tables from a separately obtained Linux file."""

from hashlib import sha256
from pathlib import Path
import re

LINUX_PINCTRL_SHA256 = '0dc2e83fd39d75e61f4e22c879425b4ce2d326fc37889042f01d9234d265a3cd'


def write_header(linux_file: Path, output: Path):
    raw = linux_file.read_bytes()
    if sha256(raw).hexdigest() != LINUX_PINCTRL_SHA256:
        raise ValueError('Expected the Linux v6.12 pinctrl-meteorlake.c listed in README.md')
    source = raw.decode('utf-8')
    tables = re.findall(
        r'^static const (struct (?:pinctrl_pin_desc|intel_padgroup|intel_community) '
        r'mtlp_\w+\[\] = \{\n.*?^\};)', source, re.MULTILINE | re.DOTALL,
    )
    if len(tables) != 7:
        raise ValueError('Unexpected Meteor Lake-P table layout')
    tables = '\n\n'.join(tables).replace('MTL_GPP(', 'MTLP_GPP(')
    tables = re.sub(r'(PINCTRL_PIN\(\d+, )"', r'\1(char *)"', tables)
    registers = dict(re.findall(r'^#define MTL_P_(\w+)\s+(0x[0-9a-f]+)$', source, re.MULTILINE))
    output.write_text('''// SPDX-License-Identifier: GPL-2.0
// Generated locally from Linux v6.12 pinctrl-meteorlake.c.
// Copyright (C) 2022 Intel Corporation. Author: Andy Shevchenko.
#ifndef VoodooGPIOMeteorLakeP_hpp
#define VoodooGPIOMeteorLakeP_hpp
#include "../VoodooGPIOIntel.hpp"
#define MTLP_GPP(r,s,e,g) { .reg_num=(r), .base=(s), .size=((e)-(s)+1), .gpio_base=(g) }
#define MTL_P_COMMUNITY(b,s,e,g) { .barno=(b), \\
    .padown_offset=%s, .padcfglock_offset=%s, .hostown_offset=%s, .is_offset=%s, .ie_offset=%s, \\
    .pin_base=(s), .npins=((e)-(s)+1), .gpps=(g), .ngpps=ARRAY_SIZE(g) }
%s

class VoodooGPIOMeteorLakeP : public VoodooGPIOIntel {
    OSDeclareDefaultStructors(VoodooGPIOMeteorLakeP);
    bool start(IOService *provider) override;
};
#endif
''' % (*(registers[k] for k in ('PAD_OWN', 'PADCFGLOCK', 'HOSTSW_OWN', 'GPI_IS', 'GPI_IE')),
       re.sub(r'^struct ', 'static struct ', tables, flags=re.MULTILINE)), encoding='utf-8')
