/*
 * Copyright (c) 2025 Alnicko Lab OU. All rights reserved.
 */

#include <stdio.h>
#include <zephyr/kernel.h>

#ifdef CONFIG_EXAMPLES_ENABLE_MAIN_EXAMPLES
#include "domain_logic.h"
#endif

int main(void)
{

#ifdef CONFIG_EXAMPLES_ENABLE_MAIN_EXAMPLES
    domain_logic_func();
#endif

    return 0;
}