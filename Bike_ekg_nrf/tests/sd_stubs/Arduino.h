#pragma once
// Minimaler Host-Ersatz. __DMB ist hier wirkungslos: keine Nebenläufigkeit testen.
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#define __DMB() do {} while (0)
