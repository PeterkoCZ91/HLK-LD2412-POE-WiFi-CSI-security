#pragma once

#include <cstddef>
#include <cstring>

inline char sensitiveAsciiLower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

inline bool sensitiveTokenEquals(const char* value, size_t length, const char* expected) {
    size_t expectedLength = std::strlen(expected);
    if (length != expectedLength) return false;
    for (size_t i = 0; i < length; i++) {
        if (sensitiveAsciiLower(value[i]) != expected[i]) return false;
    }
    return true;
}

inline bool isSensitiveKeyName(const char* key, size_t length) {
    if (key == nullptr || length == 0) return false;
    size_t tokenStart = 0;
    for (size_t i = 0; i <= length; i++) {
        bool boundary = i == length || key[i] == '_' || key[i] == '-' || key[i] == '.';
        if (!boundary) continue;
        size_t tokenLength = i - tokenStart;
        if (sensitiveTokenEquals(key + tokenStart, tokenLength, "pass") ||
            sensitiveTokenEquals(key + tokenStart, tokenLength, "password") ||
            sensitiveTokenEquals(key + tokenStart, tokenLength, "passwd") ||
            sensitiveTokenEquals(key + tokenStart, tokenLength, "token") ||
            sensitiveTokenEquals(key + tokenStart, tokenLength, "secret") ||
            sensitiveTokenEquals(key + tokenStart, tokenLength, "credential") ||
            sensitiveTokenEquals(key + tokenStart, tokenLength, "credentials") ||
            sensitiveTokenEquals(key + tokenStart, tokenLength, "pin") ||
            sensitiveTokenEquals(key + tokenStart, tokenLength, "webhook") ||
            sensitiveTokenEquals(key + tokenStart, tokenLength, "apikey")) {
            return true;
        }
        tokenStart = i + 1;
    }
    return false;
}

inline bool isSensitiveKeyName(const char* key) {
    return key != nullptr && isSensitiveKeyName(key, std::strlen(key));
}

inline bool sensitiveKeyChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
}

inline void maskSensitiveValue(char* text, size_t length, size_t valueStart) {
    while (valueStart < length && (text[valueStart] == ' ' || text[valueStart] == '\t')) {
        valueStart++;
    }
    if (valueStart >= length) return;

    char quote = (text[valueStart] == '"' || text[valueStart] == '\'')
        ? text[valueStart++] : '\0';
    for (size_t i = valueStart; i < length; i++) {
        if ((quote != '\0' && text[i] == quote) ||
            (quote == '\0' && (text[i] == '&' || text[i] == ',' ||
                              text[i] == '}' || text[i] == ']' ||
                              text[i] == ';' || text[i] == ' ' ||
                              text[i] == '\t' || text[i] == '\r' ||
                              text[i] == '\n' || text[i] == '\''))) {
            break;
        }
        text[i] = '*';
    }
}

inline void redactSensitiveText(char* text, size_t capacity) {
    if (text == nullptr || capacity == 0) return;
    size_t length = strnlen(text, capacity);

    // URI userinfo is itself credential material, regardless of key names.
    for (size_t i = 0; i + 2 < length; i++) {
        if (text[i] != ':' || text[i + 1] != '/' || text[i + 2] != '/') continue;
        size_t authority = i + 3;
        size_t end = authority;
        while (end < length && text[end] != '/' && text[end] != '?' && text[end] != '#') end++;
        size_t at = authority;
        while (at < end && text[at] != '@') at++;
        if (at < end) {
            for (size_t p = authority; p < at; p++) text[p] = '*';
        }
        i = end;
    }

    // Common log/export forms: key=value, key: value and JSON "key":"value".
    for (size_t i = 0; i < length; i++) {
        size_t keyStart = i;
        size_t keyEnd = i;
        size_t afterKey = i;
        if (text[i] == '"' || text[i] == '\'') {
            char quote = text[i];
            keyStart = ++keyEnd;
            while (keyEnd < length && text[keyEnd] != quote) keyEnd++;
            // Unmatched quote: not a real key delimiter — treat it as an
            // ordinary char and keep scanning instead of abandoning the rest
            // of the buffer unredacted.
            if (keyEnd >= length) continue;
            afterKey = keyEnd + 1;
        } else {
            if (!sensitiveKeyChar(text[i]) ||
                (i > 0 && sensitiveKeyChar(text[i - 1]))) continue;
            while (keyEnd < length && sensitiveKeyChar(text[keyEnd])) keyEnd++;
            afterKey = keyEnd;
        }

        while (afterKey < length && (text[afterKey] == ' ' || text[afterKey] == '\t')) {
            afterKey++;
        }
        if (afterKey >= length || (text[afterKey] != '=' && text[afterKey] != ':')) continue;
        if (isSensitiveKeyName(text + keyStart, keyEnd - keyStart)) {
            maskSensitiveValue(text, length, afterKey + 1);
        }
        i = keyEnd;
    }
}
