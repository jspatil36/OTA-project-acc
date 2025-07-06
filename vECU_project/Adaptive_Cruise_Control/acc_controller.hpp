#pragma once

#include <iostream>

/**
 * @brief The main entry point for the ACC application logic.
 *
 * This function will be loaded and run by the main ECU application.
 * It's declared as extern "C" to prevent C++ name mangling, which makes
 * it easier to find and load from the shared library.
 */
extern "C" void run_acc_application();
