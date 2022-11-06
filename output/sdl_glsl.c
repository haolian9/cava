#define GL_GLEXT_PROTOTYPES 1
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include "output/sdl_glsl.h"

#include <stdbool.h>
#include <stdlib.h>

#include "util.h"

SDL_Window *glWindow = NULL;
GLuint shading_program;
GLint uniform_bars;
GLint uniform_bars_count;
GLint uniform_res;
GLint uniform_bg_col;
GLint uniform_fg_col;
SDL_GLContext *glContext = NULL;

struct colors {
    uint16_t R;
    uint16_t G;
    uint16_t B;
};

static void parse_color(char *color_string, struct colors *color) {
    if (color_string[0] == '#') {
        sscanf(++color_string, "%02hx%02hx%02hx", &color->R, &color->G, &color->B);
    }
}

GLuint get_shader(GLenum, const char *);

GLuint custom_shaders(const char *, const char *);

const char *read_file(const char *);

GLuint compile_shader(GLenum type, GLsizei, const char **);
GLuint program_check(GLuint);

void init_sdl_glsl_window(int width, int height, int x, int y, char *const vertex_shader,
                          char *const fragmnet_shader) {
    if (x == -1)
        x = SDL_WINDOWPOS_UNDEFINED;

    if (y == -1)
        y = SDL_WINDOWPOS_UNDEFINED;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        exit(1);
    }

#ifdef __APPLE__
    SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
#endif

    glWindow = SDL_CreateWindow("cava", x, y, width, height,
                                SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (glWindow == NULL) {
        fprintf(stderr, "Window could not be created! SDL_Error: %s\n", SDL_GetError());
        exit(1);
    }
    SDL_GLContext glContext = SDL_GL_CreateContext(glWindow);
    if (glContext == NULL) {
        fprintf(stderr, "GLContext could not be created! SDL Error: %s\n", SDL_GetError());
        exit(1);
    }
    shading_program = custom_shaders(vertex_shader, fragmnet_shader);
    glReleaseShaderCompiler();
    if (shading_program == 0) {
        fprintf(stderr, "could not compile shaders: %s\n", SDL_GetError());
        exit(1);
    }

    glUseProgram(shading_program);

    uniform_bars = glGetUniformLocation(shading_program, "bars");
    uniform_bars_count = glGetUniformLocation(shading_program, "bars_count");
    uniform_res = glGetUniformLocation(shading_program, "u_resolution");
    uniform_bg_col = glGetUniformLocation(shading_program, "bg_color");
    uniform_fg_col = glGetUniformLocation(shading_program, "fg_color");
    glUniform3f(uniform_res, (float)width, (float)height, 0.0f);

    if (glGetError() != 0) {
        fprintf(stderr, "glError: %#08x\n", glGetError());
        exit(1);
    }
}

void init_sdl_glsl_surface(int *w, int *h, char *const fg_color_string,
                           char *const bg_color_string) {
    struct colors color = {0};

    parse_color(bg_color_string, &color);
    glUniform3f(uniform_bg_col, (float)color.R / 255.0, (float)color.G / 255.0,
                (float)color.B / 255.0);

    parse_color(fg_color_string, &color);
    glUniform3f(uniform_fg_col, (float)color.R / 255.0, (float)color.G / 255.0,
                (float)color.B / 255.0);

    SDL_GetWindowSize(glWindow, w, h);
    glUniform3f(uniform_res, (float)*w, (float)*h, 0.0f);
}

int draw_sdl_glsl(int bars_count, const float bars[], int frame_time, int re_paint,
                  int continuous_rendering) {

    int rc = 0;
    SDL_Event event;

    if (re_paint || continuous_rendering) {
        glUniform1fv(uniform_bars, bars_count, bars);
        glUniform1i(uniform_bars_count, bars_count);

        glClear(GL_COLOR_BUFFER_BIT);
        glRectf(-1.0, -1.0, 1.0, 1.0);
        SDL_GL_SwapWindow(glWindow);
    }
    SDL_Delay(frame_time);

    SDL_PollEvent(&event);
    if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        glViewport(0, 0, event.window.data1, event.window.data2);
        glUniform3f(uniform_res, event.window.data1, event.window.data2, 0.0f);
    }
    if (event.type == SDL_QUIT)
        rc = -2;

    return rc;
}

// general: cleanup
void cleanup_sdl_glsl(void) {
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(glWindow);
    SDL_Quit();
}

const char *read_file(const char *filename) {
    long length = 0;
    char *result = NULL;
    FILE *file = fopen(filename, "r");
    if (file) {
        int status = fseek(file, 0, SEEK_END);
        if (status != 0) {
            fclose(file);
            return NULL;
        }
        length = ftell(file);
        status = fseek(file, 0, SEEK_SET);
        if (status != 0) {
            fclose(file);
            return NULL;
        }
        result = malloc((length + 1) * sizeof(char));
        if (result) {
            size_t actual_length = fread(result, sizeof(char), length, file);
            result[actual_length++] = '\0';
        }
        fclose(file);
        return result;
    }
    fprintf(stderr, "Couldn't open shader file %s", filename);
    exit(1);
    return NULL;
}

GLuint custom_shaders(const char *vsPath, const char *fsPath) {
    GLuint vertexShader;
    GLuint fragmentShader;

    vertexShader = get_shader(GL_VERTEX_SHADER, vsPath);
    fragmentShader = get_shader(GL_FRAGMENT_SHADER, fsPath);

    shading_program = glCreateProgram();

    glAttachShader(shading_program, vertexShader);
    glAttachShader(shading_program, fragmentShader);

    glLinkProgram(shading_program);

    // Error Checking
    GLuint status;
    status = program_check(shading_program);
    if (status == GL_FALSE)
        return 0;
    return shading_program;
}

GLuint get_shader(GLenum eShaderType, const char *filename) {

    const char *shaderSource = read_file(filename);
    GLuint shader = compile_shader(eShaderType, 1, &shaderSource);
    return shader;
}

GLuint compile_shader(GLenum type, GLsizei nsources, const char **sources) {

    GLuint shader;
    GLint success, len;
    GLsizei i, srclens[nsources];

    for (i = 0; i < nsources; ++i)
        srclens[i] = (GLsizei)strlen(sources[i]);

    shader = glCreateShader(type);
    glShaderSource(shader, nsources, sources, srclens);
    glCompileShader(shader);

    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        if (len > 1) {
            char *log;
            log = malloc(len);
            glGetShaderInfoLog(shader, len, NULL, log);
            fprintf(stderr, "%s\n\n", log);
            free(log);
        }
        fprintf(stderr, "Error compiling shader.\n");
        exit(1);
    }
    return shader;
}

GLuint program_check(GLuint program) {
    // Error Checking
    GLint status;
    glValidateProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status) {
        GLint len;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
        if (len > 1) {
            char *log;
            log = malloc(len);
            glGetProgramInfoLog(program, len, &len, log);
            fprintf(stderr, "%s\n\n", log);
            free(log);
        }
        SDL_Log("Error linking shader default program.\n");
        return GL_FALSE;
    }
    return GL_TRUE;
}