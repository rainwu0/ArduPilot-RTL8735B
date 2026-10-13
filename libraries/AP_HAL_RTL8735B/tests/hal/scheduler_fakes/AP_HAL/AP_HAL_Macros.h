#pragma once

#define CLASS_NO_COPY(c) c(const c &other) = delete; c &operator=(const c&) = delete
