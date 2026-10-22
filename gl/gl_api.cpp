#include "gl/gl_api.h"

#include <cstring>

namespace glapi {

#define GLAPI_DEFINE(ret, name, params) ret(APIENTRY* name) params = nullptr;
GL_FUNCTIONS(GLAPI_DEFINE)
GL_OPTIONAL_FUNCTIONS(GLAPI_DEFINE)
#undef GLAPI_DEFINE

bool Load(void* (*get)(const char*), const char** missing) {
#define GLAPI_LOAD(ret, name, params)                                       \
    name = reinterpret_cast<ret(APIENTRY*) params>(get(#name));             \
    if (!name) {                                                            \
        if (missing) *missing = #name;                                      \
        return false;                                                       \
    }
    GL_FUNCTIONS(GLAPI_LOAD)
#undef GLAPI_LOAD
#define GLAPI_LOAD_OPTIONAL(ret, name, params) name = reinterpret_cast<ret(APIENTRY*) params>(get(#name));
    GL_OPTIONAL_FUNCTIONS(GLAPI_LOAD_OPTIONAL)
#undef GLAPI_LOAD_OPTIONAL
    return true;
}

bool HasExtension(const char* want) {
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; i++) {
        const char* e = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, GLuint(i)));
        if (e && std::strcmp(e, want) == 0) return true;
    }
    return false;
}

}  // namespace glapi
