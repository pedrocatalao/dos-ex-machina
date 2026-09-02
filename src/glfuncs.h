/* glfuncs.h — the GL 3.3 entry points DXM uses, in one list.
 *
 * macOS exposes the whole core profile through <OpenGL/gl3.h>, so nothing
 * has to be loaded there.  Everywhere else, the system's libGL/opengl32
 * exports only GL 1.1 and every function past that has to be fetched at run
 * time from the driver.  Rather than take a dependency on a generated loader
 * for forty-four functions, this lists them once and the two macros below
 * build the pointers and the loader from it.
 *
 * If gpu.c starts using a function that is not here, the link fails with the
 * name of the one that is missing - which is the failure you want. */

/*      return type          name                     parameter list        */
GLF(void,   glActiveTexture,           (GLenum))
GLF(void,   glAttachShader,            (GLuint, GLuint))
GLF(void,   glBindBuffer,              (GLenum, GLuint))
GLF(void,   glBindFramebuffer,         (GLenum, GLuint))
GLF(void,   glBindVertexArray,         (GLuint))
GLF(void,   glBufferData,              (GLenum, GLsizeiptr, const void *, GLenum))
GLF(void,   glCompileShader,           (GLuint))
GLF(GLuint, glCreateProgram,           (void))
GLF(GLuint, glCreateShader,            (GLenum))
GLF(void,   glDeleteShader,            (GLuint))
GLF(void,   glEnableVertexAttribArray, (GLuint))
GLF(void,   glFramebufferTexture2D,    (GLenum, GLenum, GLenum, GLuint, GLint))
GLF(void,   glGenBuffers,              (GLsizei, GLuint *))
GLF(void,   glGenFramebuffers,         (GLsizei, GLuint *))
GLF(void,   glGenVertexArrays,         (GLsizei, GLuint *))
GLF(void,   glGetProgramInfoLog,       (GLuint, GLsizei, GLsizei *, GLchar *))
GLF(void,   glGetProgramiv,            (GLuint, GLenum, GLint *))
GLF(void,   glGetShaderInfoLog,        (GLuint, GLsizei, GLsizei *, GLchar *))
GLF(void,   glGetShaderiv,             (GLuint, GLenum, GLint *))
GLF(GLint,  glGetUniformLocation,      (GLuint, const GLchar *))
GLF(void,   glLinkProgram,             (GLuint))
GLF(void,   glShaderSource,            (GLuint, GLsizei, const GLchar *const *, const GLint *))
GLF(void,   glUniform1f,               (GLint, GLfloat))
GLF(void,   glUniform1fv,              (GLint, GLsizei, const GLfloat *))
GLF(void,   glUniform1i,               (GLint, GLint))
GLF(void,   glUniform2f,               (GLint, GLfloat, GLfloat))
GLF(void,   glUniform3fv,              (GLint, GLsizei, const GLfloat *))
GLF(void,   glUniform4f,               (GLint, GLfloat, GLfloat, GLfloat, GLfloat))
GLF(void,   glUniform4fv,              (GLint, GLsizei, const GLfloat *))
GLF(void,   glUseProgram,              (GLuint))
GLF(void,   glVertexAttribPointer,     (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *))

/* The rest are GL 1.1 and are exported by the system library everywhere, so
 * they are deliberately NOT in this list: glBindTexture, glBlendFunc,
 * glClear, glClearColor, glDisable, glDrawArrays, glEnable, glPixelStorei,
 * glReadPixels, glTexImage2D, glTexParameteri, glViewport. */
