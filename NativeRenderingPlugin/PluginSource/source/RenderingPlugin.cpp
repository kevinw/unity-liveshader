// Example low level rendering Unity plugin

#include "PlatformBase.h"
#include "RenderAPI.h"

#include <assert.h>
#include <math.h>
#include <map>


mutex debugLogMutex;
static DebugLogFuncPtr s_debugLogFunc = nullptr;
DebugLogFuncPtr GetDebugFunc() { return s_debugLogFunc; }

static string s_shaderIncludePath;
string GetShaderIncludePath() { return s_shaderIncludePath; }


// --------------------------------------------------------------------------
// SetTimeFromUnity, an example function we export which is called by one of the scripts.

static float g_Time;

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetTimeFromUnity (float t) { g_Time = t; }



// --------------------------------------------------------------------------
// SetTextureFromUnity, an example function we export which is called by one of the scripts.

static void* g_TextureHandle = NULL;
static int   g_TextureWidth  = 0;
static int   g_TextureHeight = 0;

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetTextureFromUnity(void* textureHandle, int w, int h)
{
	// A script calls this at initialization time; just remember the texture pointer here.
	// Will update texture pixels each frame from the plugin rendering event (texture update
	// needs to happen on the rendering thread).
	g_TextureHandle = textureHandle;
	g_TextureWidth = w;
	g_TextureHeight = h;
}




// --------------------------------------------------------------------------
// UnitySetInterfaces

static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType);

static IUnityInterfaces* s_UnityInterfaces = NULL;
static IUnityGraphics* s_Graphics = NULL;

extern "C" void	UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginLoad(IUnityInterfaces* unityInterfaces)
{
	s_UnityInterfaces = unityInterfaces;
	s_Graphics = s_UnityInterfaces->Get<IUnityGraphics>();
	s_Graphics->RegisterDeviceEventCallback(OnGraphicsDeviceEvent);
	
	// Run OnGraphicsDeviceEvent(initialize) manually on plugin load
	OnGraphicsDeviceEvent(kUnityGfxDeviceEventInitialize);
}


extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetCallbackFunctions(DebugLogFuncPtr debugLogFunc) {
	{
		lock_guard<mutex> guard(debugLogMutex);
		s_debugLogFunc = debugLogFunc;
	}
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginUnload()
{
	s_Graphics->UnregisterDeviceEventCallback(OnGraphicsDeviceEvent);

	// clear the debug log function
	{
		lock_guard<mutex> guard(debugLogMutex);
		s_debugLogFunc = nullptr;
	}
}



// --------------------------------------------------------------------------
// GraphicsDeviceEvent


static RenderAPI* s_CurrentAPI = NULL;
static UnityGfxRenderer s_DeviceType = kUnityGfxRendererNull;

static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType)
{
	// Create graphics API implementation upon initialization
	if (eventType == kUnityGfxDeviceEventInitialize) {
		assert(s_CurrentAPI == NULL);
		s_DeviceType = s_Graphics->GetRenderer();
		s_CurrentAPI = CreateRenderAPI(s_DeviceType);
		s_CurrentAPI->Initialize();
	}

	// Let the implementation process the device related events
	if (s_CurrentAPI) {
		s_CurrentAPI->ProcessDeviceEvent(eventType, s_UnityInterfaces);
	}

	// Cleanup graphics API implementation upon shutdown
	if (eventType == kUnityGfxDeviceEventShutdown) {
		delete s_CurrentAPI;
		s_CurrentAPI = NULL;
		s_DeviceType = kUnityGfxRendererNull;
	}
}


#define UNITY_FUNC UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API

typedef LiveMaterial* NativePtr;

extern "C" {

	NativePtr UNITY_FUNC CreateLiveMaterial() {
		assert(s_CurrentAPI);
		return s_CurrentAPI->CreateLiveMaterial();
	}

	void UNITY_FUNC DestroyLiveMaterial(LiveMaterial* liveMaterial) {
		s_CurrentAPI->DestroyLiveMaterial(liveMaterial->id());
	}

	int UNITY_FUNC GetLiveMaterialId(LiveMaterial* liveMaterial) { return liveMaterial->id(); }

	void UNITY_FUNC SetShaderSource(LiveMaterial* liveMaterial, const char* fragSrc, const char* fragEntry, const char* vertSrc, const char* vertEntry) { liveMaterial->SetShaderSource(fragSrc, fragEntry, vertSrc, vertEntry); }
	void UNITY_FUNC SubmitUniforms(LiveMaterial* liveMaterial, int uniformsIndex) { liveMaterial->SubmitUniforms(uniformsIndex); }

	void UNITY_FUNC SetFloat(LiveMaterial* liveMaterial, const char* name, float value) { liveMaterial->SetFloat(name, value); }
	void UNITY_FUNC SetVector4(LiveMaterial* liveMaterial, const char* name, float* value) { liveMaterial->SetVector4(name, value); }
	void UNITY_FUNC SetMatrix(LiveMaterial* liveMaterial, const char* name, float* value) { liveMaterial->SetMatrix(name, value); }
	void UNITY_FUNC SetFloatArray(LiveMaterial* liveMaterial, const char* name, float* value, int numFloats) { liveMaterial->SetFloatArray(name, value, numFloats); }

	float UNITY_FUNC GetFloat(LiveMaterial* liveMaterial, const char* name) {
		float value = 0;
		liveMaterial->GetFloat(name, &value);
		return value;
	}

	void UNITY_FUNC GetVector4(LiveMaterial* liveMaterial, const char* name, float* value) { liveMaterial->GetVector4(name, value); }
	void UNITY_FUNC GetMatrix(LiveMaterial* liveMaterial, const char* name, float* value) { liveMaterial->GetMatrix(name, value); }

	void UNITY_FUNC SetShaderIncludePath(const char* includePath) { s_shaderIncludePath = includePath; }
}

static void DrawColoredTriangle(int uniformIndex)
{
	// Draw a colored triangle. Note that colors will come out differently
	// in D3D and OpenGL, for example, since they expect color bytes
	// in different ordering.
	struct MyVertex {
		float x, y, z;
		unsigned int color;
	};
	MyVertex verts[3] = {
		{ -0.5f, -0.25f,  0, 0xFFff0000 },
		{ 0.5f, -0.25f,  0, 0xFF00ff00 },
		{ 0,     0.5f ,  0, 0xFF0000ff },
	};

	// Transformation matrix: rotate around Z axis based on time.
	float phi = g_Time; // time set externally from Unity script
	float cosPhi = cosf(phi);
	float sinPhi = sinf(phi);
	float worldMatrix[16] = {
		cosPhi,-sinPhi,0,0,
		sinPhi,cosPhi,0,0,
		0,0,1,0,
		0,0,0.7f,1,
	};

	s_CurrentAPI->DrawSimpleTriangles(worldMatrix, 1, verts);
}


static void ModifyTexturePixels()
{
	void* textureHandle = g_TextureHandle;
	int width = g_TextureWidth;
	int height = g_TextureHeight;
	if (!textureHandle)
		return;

	int textureRowPitch;
	void* textureDataPtr = (unsigned char*)s_CurrentAPI->BeginModifyTexture(textureHandle, width, height, &textureRowPitch);
	if (!textureDataPtr)
		return;

	const float t = g_Time * 4.0f;

	unsigned char* dst = (unsigned char*)textureDataPtr;
	for (int y = 0; y < height; ++y)
	{
		unsigned char* ptr = dst;
		for (int x = 0; x < width; ++x)
		{
			// Simple "plasma effect": several combined sine waves
			int vv = int(
				(127.0f + (127.0f * sinf(x / 7.0f + t))) +
				(127.0f + (127.0f * sinf(y / 5.0f - t))) +
				(127.0f + (127.0f * sinf((x + y) / 6.0f - t))) +
				(127.0f + (127.0f * sinf(sqrtf(float(x*x + y*y)) / 4.0f - t)))
				) / 4;

			// Write the texture pixel
			ptr[0] = vv;
			ptr[1] = vv;
			ptr[2] = vv;
			ptr[3] = vv;

			// To next pixel (our pixels are 4 bpp)
			ptr += 4;
		}

		// To next image row
		dst += textureRowPitch;
	}

	s_CurrentAPI->EndModifyTexture(textureHandle, width, height, textureRowPitch, textureDataPtr);
}


static void UNITY_INTERFACE_API OnRenderEvent(int packedValue)
{
	// Unknown / unsupported graphics device type? Do nothing
	if (s_CurrentAPI == NULL)
		return;

	int16_t uniformIndex = packedValue & 0xffff;
	int16_t id = (packedValue >> 16) & 0xffff;

	//DrawColoredTriangle(uniformIndex);
	lock_guard<mutex> guard(s_CurrentAPI->materialsMutex);
	auto liveMaterial = s_CurrentAPI->GetLiveMaterialByIdLocked(id);
	if (liveMaterial) {
		liveMaterial->Draw(uniformIndex);
	}
	//ModifyTexturePixels();
}


extern "C" UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetRenderEventFunc()
{
	return OnRenderEvent;
}

