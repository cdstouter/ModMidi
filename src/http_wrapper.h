/* 
 * File:   http_wrapper.h
 * Author: caleb
 *
 * Created on December 13, 2017, 1:14 PM
 */

#ifndef HTTP_WRAPPER_H
#define HTTP_WRAPPER_H

// this file wraps http.h with a GCC directive to not print warnings
#ifdef __GNUC__
#pragma GCC system_header
#endif

#include "http.h"

#endif /* HTTP_WRAPPER_H */

