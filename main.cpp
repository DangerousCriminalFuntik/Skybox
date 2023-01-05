#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdexcept>
#include <memory>
#include <array>
#include <vector>
#include <fstream>
#include <string>
#include <string_view>
#include <cmath>
#include <cassert>

#define GLEW_STATIC
#include <GL/glew.h>
#include <GL/wglew.h>

#define GLM_FORCE_RADIANS
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/ext.hpp>

#include <gli/gli.hpp>

namespace {

	static const GLfloat vertices[] =
	{
		-1.0f, -1.0f, -1.0f,
		-1.0f, -1.0f,  1.0f,
		-1.0f,  1.0f, -1.0f,
		-1.0f,  1.0f,  1.0f,
		 1.0f, -1.0f, -1.0f,
		 1.0f, -1.0f,  1.0f,
		 1.0f,  1.0f, -1.0f,
		 1.0f,  1.0f,  1.0f
	};

	static const GLushort indices[] =
	{
		0, 1, 2, 3, 6, 7, 4, 5,         // First strip
		2, 6, 0, 4, 1, 5, 3, 7          // Second strip
	};

	struct Transform
	{
		glm::mat4 MVP;
	};

	namespace buffer
	{
		enum type
		{
			VERTEX,
			ELEMENT,
			TRANSFORM,
			MAX
		};
	}

	HWND hwnd = nullptr; // The Window handle.
	HDC hdc = nullptr; // Device context.
	HGLRC hglrc = nullptr; // Rendering context.
	int windowWidth{ 1920 };
	int windowHeight{ 1080 };
	// is used to rotate the entire scene.
	glm::vec2 rotation(0.0f, 0.0f);
	GLuint pipeline{};
	GLuint render_program{};
	GLuint vao{};
	std::array<GLuint, buffer::MAX> buffers{};
	GLint blockSize{};
	GLuint skyboxTexture{};
}

// Function Prototypes
bool Init();
void InitGL();
void InitProgram();
void InitBuffer();
void InitVertexArray();
void InitTexture();
void RenderFrame();
void Shutdown();
void CheckShader(GLuint shader);
void CheckProgram(GLuint program);
GLuint CreateShader(std::string_view filename, GLenum shaderType);
GLuint CreateProgram(const std::vector<GLuint>& shaders);
GLuint CreateTexture(const char* filename);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPSTR /*lpCmdLine*/, int /*nShowCmd*/)
{
	static constexpr const char* APP_TITLE = "Skybox DDS";
	static constexpr const char* WINDOW_CLASS = "GLWindowClass";

	WNDCLASSEX wcex = {
		.cbSize = sizeof(WNDCLASSEX),
		.style = CS_HREDRAW | CS_VREDRAW,
		.lpfnWndProc = WndProc,
		.hInstance = hInstance,
		.hIcon = LoadIcon(nullptr, IDI_APPLICATION),
		.hCursor = LoadCursor(nullptr, IDC_ARROW),
		.lpszClassName = WINDOW_CLASS,
		.hIconSm = LoadIcon(nullptr, IDI_APPLICATION)
	};

	if (!RegisterClassEx(&wcex))
		return 0;

	DWORD wndExStyle = WS_EX_OVERLAPPEDWINDOW;
	DWORD wndStyle = WS_OVERLAPPEDWINDOW;

	RECT rc = {
		.left = 0L,
		.top = 0L,
		.right = static_cast<LONG>(windowWidth),
		.bottom = static_cast<LONG>(windowHeight)
	};
	AdjustWindowRectEx(&rc, wndStyle, FALSE, wndExStyle);

	hwnd = CreateWindowEx(wndExStyle, WINDOW_CLASS, APP_TITLE, wndStyle,
		CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
		nullptr, nullptr, hInstance, nullptr);

	if (!hwnd)
		return 0;

	ShowWindow(hwnd, SW_SHOWNORMAL);
	UpdateWindow(hwnd);

	if (!Init())
		return 0;

	MSG msg = {};
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			RenderFrame();
			SwapBuffers(hdc);
		}
	}

	Shutdown();
	UnregisterClass(WINDOW_CLASS, hInstance);

	return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	static POINT lastMousePos = {};		// Last mouse position.
	static POINT currentMousePos = {};	// Current mouse position.
	static bool isMouseActive{ false };	// Is the left mouse button down.

	switch (message)
	{
	case WM_DESTROY:
	case WM_CLOSE:
		PostQuitMessage(0);
		return 0;

	case WM_SIZE:
		windowWidth = LOWORD(lParam);
		windowHeight = HIWORD(lParam);
		break;

	case WM_KEYDOWN:
		switch (wParam)
		{
		case VK_ESCAPE:
			PostQuitMessage(0);
			break;

		default:
			break;
		}
		break;

	case WM_LBUTTONDOWN:
		lastMousePos.x = currentMousePos.x = LOWORD(lParam);
		lastMousePos.y = currentMousePos.y = HIWORD(lParam);
		isMouseActive = true;
		break;

	case WM_LBUTTONUP:
		isMouseActive = false;
		break;

	case WM_MOUSEMOVE:
		currentMousePos.x = LOWORD(lParam);
		currentMousePos.y = HIWORD(lParam);

		if (isMouseActive)
		{
			rotation.x -= (currentMousePos.x - lastMousePos.x);
			rotation.y -= (currentMousePos.y - lastMousePos.y);
		}

		lastMousePos.x = currentMousePos.x;
		lastMousePos.y = currentMousePos.y;
		break;

	default:
		break;
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}

bool Init()
{
	try
	{
		InitGL();
		InitProgram();
		InitBuffer();
		InitVertexArray();
		InitTexture();
		return true;
	}
	catch (const std::exception& e)
	{
		MessageBox(nullptr, e.what(), "Exception", MB_OK | MB_ICONERROR);
		return false;
	}
}

void InitGL()
{
	hdc = GetDC(hwnd);
	if (!hdc)
		throw std::runtime_error("Failed to GetDC().");

	PIXELFORMATDESCRIPTOR pfd = {
		.nSize = sizeof(PIXELFORMATDESCRIPTOR),
		.nVersion = 1,
		.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
		.iPixelType = PFD_TYPE_RGBA,
		.cColorBits = 32,
		.cDepthBits = 24,
	};

	auto pixelFormat = ChoosePixelFormat(hdc, &pfd);
	if (pixelFormat == 0)
		throw std::runtime_error("Failed to ChoosePixelFormat().");

	if (!SetPixelFormat(hdc, pixelFormat, &pfd))
		throw std::runtime_error("Failed to SetPixelFormat().");

	auto tempCtx = wglCreateContext(hdc);
	if (!tempCtx || !wglMakeCurrent(hdc, tempCtx))
		throw std::runtime_error("Creating temp render context failed");

	if (auto error = glewInit(); error != GLEW_OK)
		throw std::runtime_error("GLEW Error: " + std::to_string(error));

	wglMakeCurrent(nullptr, nullptr);
	wglDeleteContext(tempCtx);

	constexpr std::array<int, 7> attribs
	{
		WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
		WGL_CONTEXT_MINOR_VERSION_ARB, 6,
		WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
		0
	};

	hglrc = wglCreateContextAttribsARB(hdc, 0, attribs.data());
	if (!hglrc || !wglMakeCurrent(hdc, hglrc))
		throw std::runtime_error("Creating render context failed");
}

void InitProgram()
{
	auto vs = CreateShader("skybox.vert", GL_VERTEX_SHADER);
	auto fs = CreateShader("skybox.frag", GL_FRAGMENT_SHADER);
	render_program = CreateProgram({ vs, fs });

	glCreateProgramPipelines(1, &pipeline);
	glUseProgramStages(pipeline, GL_VERTEX_SHADER_BIT | GL_FRAGMENT_SHADER_BIT, render_program);
}

void InitBuffer()
{
	GLint alignment{};
	glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &alignment);
	blockSize = glm::max(GLint(sizeof(Transform)), alignment);

	glCreateBuffers(buffer::MAX, buffers.data());
	glNamedBufferStorage(buffers[buffer::VERTEX], sizeof(vertices), vertices, 0);
	glNamedBufferStorage(buffers[buffer::ELEMENT], sizeof(indices), indices, 0);
	glNamedBufferStorage(buffers[buffer::TRANSFORM], blockSize, nullptr, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
}

void InitVertexArray()
{
	glCreateVertexArrays(1, &vao);
	glVertexArrayAttribBinding(vao, 0, 0);
	glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
	glEnableVertexArrayAttrib(vao, 0);

	glVertexArrayVertexBuffer(vao, 0, buffers[buffer::VERTEX], 0, 3 * sizeof(GLfloat));
	glVertexArrayElementBuffer(vao, buffers[buffer::ELEMENT]);
}

void InitTexture()
{
	skyboxTexture = CreateTexture("StockholmRoyalCastle.dds");
}

GLuint CreateTexture(const char* filename)
{
	gli::texture Texture = gli::load(filename);
	if (Texture.empty())
		return 0;

	gli::gl GL(gli::gl::PROFILE_GL33);
	gli::gl::format const& Format = GL.translate(Texture.format(), Texture.swizzles());
	GLenum Target = GL.translate(Texture.target());
	//assert(gli::is_compressed(Texture.format()) && Target == gli::TARGET_CUBE);

	GLuint TextureName = 0;
	glGenTextures(1, &TextureName);
	glBindTexture(Target, TextureName);
	glTexParameteri(Target, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(Target, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(Texture.levels() - 1));
	glTexParameteriv(Target, GL_TEXTURE_SWIZZLE_RGBA, &Format.Swizzles[0]);
	glTexStorage2D(Target, static_cast<GLint>(Texture.levels()), Format.Internal, Texture.extent().x, Texture.extent().y);
	for (std::size_t Level = 0; Level < Texture.levels(); ++Level)
	{
		glm::tvec3<GLsizei> Extent(Texture.extent(Level));
		for (int face = 0; face < 6; ++face)
		{
			glCompressedTexSubImage2D(
				GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, static_cast<GLint>(Level), 0, 0, Extent.x, Extent.y,
				Format.Internal, static_cast<GLsizei>(Texture.size(Level)), Texture.data(0, face, Level));
		}
	}

	return TextureName;
}

void RenderFrame()
{
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDisable(GL_CULL_FACE);

	glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

	{
		auto transform = static_cast<Transform*>(glMapNamedBufferRange(buffers[buffer::TRANSFORM], 0,
			blockSize, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT | GL_MAP_INVALIDATE_BUFFER_BIT));

		auto aspectRatio = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
		glm::mat4 Projection = glm::perspective(glm::pi<float>() * 0.25f, aspectRatio, 0.1f, 1000.0f);
		glm::mat4 ViewTranslate = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f));
		glm::mat4 ViewRotateX = glm::rotate(glm::mat4(1.0f), glm::radians(-rotation.y), glm::vec3(1.0f, 0.0f, 0.0f));
		glm::mat4 View = glm::rotate(ViewRotateX, glm::radians(-rotation.x), glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 Model = glm::scale(glm::mat4(1.0f), glm::vec3(500.0f));

		transform->MVP = Projection * View * Model;

		glUnmapNamedBuffer(buffers[buffer::TRANSFORM]);
	}

	glViewportIndexedf(0, 0.0f, 0.0f, static_cast<GLfloat>(windowWidth), static_cast<GLfloat>(windowHeight));
	glClearBufferfv(GL_COLOR, 0, &glm::vec4(0.3f, 0.5f, 0.9f, 1.0f)[0]);
	glClearBufferfv(GL_DEPTH, 0, &glm::vec4(1.0f)[0]);

	glBindProgramPipeline(pipeline);
	glBindVertexArray(vao);
	glBindBufferRange(GL_UNIFORM_BUFFER, 1, buffers[buffer::TRANSFORM], 0, blockSize);
	glBindTextures(0, 1, &skyboxTexture);

	glDrawElements(GL_TRIANGLE_STRIP, 8, GL_UNSIGNED_SHORT, nullptr);
	glDrawElements(GL_TRIANGLE_STRIP, 8, GL_UNSIGNED_SHORT, (const void*)(8 * sizeof(GLushort)));
}

GLuint CreateShader(std::string_view filename, GLenum shaderType)
{
	auto source = [filename]() {
		std::string result;
		std::ifstream stream(filename.data());

		if (!stream.is_open())
		{
			throw std::runtime_error("Could not open file: " + std::string(filename));

		}

		stream.seekg(0, std::ios::end);
		result.reserve((size_t)stream.tellg());
		stream.seekg(0, std::ios::beg);

		result.assign((std::istreambuf_iterator<char>(stream)),
			std::istreambuf_iterator<char>());

		return result;
	}();

	const GLchar* src = source.c_str();

	GLuint shader{ glCreateShader(shaderType) };
	glShaderSource(shader, 1, &src, nullptr);
	glCompileShader(shader);
	CheckShader(shader);

	return shader;
}

GLuint CreateProgram(const std::vector<GLuint>& shaders)
{
	GLuint program{ glCreateProgram() };
	glProgramParameteri(program, GL_PROGRAM_SEPARABLE, GL_TRUE);

	for (const auto& shader : shaders)
	{
		glAttachShader(program, shader);
	}

	glLinkProgram(program);
	CheckProgram(program);

	for (const auto& shader : shaders)
	{
		glDetachShader(program, shader);
		glDeleteShader(shader);
	}

	return program;
}

void CheckShader(GLuint shader)
{
	GLint isCompiled{};
	glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);

	GLint maxLength{};
	glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);
	
	if (maxLength > 0
#ifdef NDEBUG
		&& isCompiled == GL_FALSE
#endif // NDEBUG
		)
	{
		std::vector<char> buffer(maxLength);
		glGetShaderInfoLog(shader, maxLength, nullptr, buffer.data());
		glDeleteShader(shader);

		throw std::runtime_error("Error compiled:\n" + std::string(buffer.begin(), buffer.end()));
	}
}

void CheckProgram(GLuint program)
{
	GLint isLinked{};
	glGetProgramiv(program, GL_LINK_STATUS, &isLinked);

	GLint maxLength{};
	glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);
	
	if (maxLength > 0
#ifdef NDEBUG
		&& isLinked == GL_FALSE
#endif // NDEBUG
		)
	{
		std::vector<char> buffer(maxLength);
		glGetProgramInfoLog(program, maxLength, nullptr, buffer.data());
		glDeleteProgram(program);

		throw std::runtime_error("Error linking:\n" + std::string(buffer.begin(), buffer.end()));
	}
}

void Shutdown()
{
	glDeleteProgram(render_program);
	glDeleteProgramPipelines(1, &pipeline);
	glDeleteBuffers(buffer::MAX, buffers.data());
	glDeleteVertexArrays(1, &vao);
	glDeleteTextures(1, &skyboxTexture);

	if (hwnd)
	{
		if (hdc)
		{
			if (hglrc)
			{
				wglMakeCurrent(hdc, nullptr);
				wglDeleteContext(hglrc);
				hglrc = nullptr;
			}

			ReleaseDC(hwnd, hdc);
			hdc = nullptr;
		}

		DestroyWindow(hwnd);
		hwnd = nullptr;
	}
}
