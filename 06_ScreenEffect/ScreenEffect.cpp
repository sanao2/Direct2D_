#include <windows.h>
#include <assert.h>
#include <wrl.h>  // ComPtr 사용을 위한 헤더
#include <comdef.h>

#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")

#include <d2d1_3.h> //ID2D1Factory8,ID2D1DeviceContext7
#pragma comment(lib, "d2d1.lib")

#include <dxgi1_6.h> // IDXGIFactory7
#pragma comment(lib, "dxgi.lib")

#include <wincodec.h>
#pragma comment(lib,"windowscodecs.lib")

#include <d2d1effects_2.h>
#pragma comment(lib, "dxguid.lib")

#include <dxgidebug.h>  // DXGIGetDebugInterface1

#pragma comment(lib, "dxguid.lib")

using namespace Microsoft::WRL;

// 전역 변수
HWND g_hwnd = nullptr;
ComPtr<ID3D11Device> g_d3dDevice;
ComPtr<IDXGISwapChain1> g_dxgiSwapChain;
ComPtr<ID2D1DeviceContext7> g_d2dDeviceContext;
ComPtr<ID2D1Bitmap1> g_d2dBitmapTarget;

// For ImageDraw
ComPtr<IWICImagingFactory> g_wicImagingFactory;
ComPtr<ID2D1Bitmap1> g_d2dBitmapMushroom;

// Effect
ComPtr<ID2D1Effect> g_skewEffect;
ComPtr<ID2D1Effect> g_shadowEffect;
ComPtr<ID2D1Effect> g_sceneEffect;

//SceneBuffer
ComPtr<ID2D1Bitmap1> g_d2dBitmapScene;
ComPtr<ID2D1Bitmap1> g_d2dBitmapBlood;

UINT g_width = 1024;
UINT g_height = 768;
bool g_resized = false;
bool g_useScreenEffect = false;

void Initialize(HWND hwnd);
void Uninitialize();

// WIC를 통해 PNG 등을 로드하여 ID2D1Bitmap1**으로 반환
HRESULT CreateBitmapFromFile(const wchar_t* path, ID2D1Bitmap1** outBitmap) 
{
	ComPtr<IWICBitmapDecoder>     decoder;
	ComPtr<IWICBitmapFrameDecode> frame;
	ComPtr<IWICFormatConverter>   converter;

	// ① 디코더 생성
	HRESULT hr = g_wicImagingFactory->CreateDecoderFromFilename(
		path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
	if (FAILED(hr)) return hr;

	// ② 첫 프레임 얻기
	hr = decoder->GetFrame(0, &frame);
	if (FAILED(hr)) return hr;

	// ③ 포맷 변환기 생성
	hr = g_wicImagingFactory->CreateFormatConverter(&converter);
	if (FAILED(hr)) return hr;

	// ④ GUID_WICPixelFormat32bppPBGRA로 변환
	hr = converter->Initialize(
		frame.Get(),
		GUID_WICPixelFormat32bppPBGRA,
		WICBitmapDitherTypeNone,
		nullptr,
		0.0f,
		WICBitmapPaletteTypeCustom
	);
	if (FAILED(hr)) return hr;

	// ⑤ Direct2D 비트맵 속성 (premultiplied alpha, B8G8R8A8_UNORM)
	D2D1_BITMAP_PROPERTIES1 bmpProps = D2D1::BitmapProperties1(
		D2D1_BITMAP_OPTIONS_NONE,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
	);

	// ⑥ DeviceContext에서 WIC 비트맵으로부터 D2D1Bitmap1 생성
	hr = g_d2dDeviceContext->CreateBitmapFromWicBitmap(converter.Get(), &bmpProps, outBitmap);
	return hr;
}


LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	case WM_KEYDOWN:
		if (wParam == VK_SPACE)
			g_useScreenEffect = !g_useScreenEffect;
		break;
	case WM_SIZE:
	{
		if (wParam == SIZE_MINIMIZED)
			break; // 최소화는 무시

		UINT width = LOWORD(lParam); // 새 너비
		UINT height = HIWORD(lParam); // 새 높이			
		if (g_width != width || g_height != height)
		{
			g_width = width;
			g_height = height;
			g_resized = true;
		}
	}
	break;
	default:
		break;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

// 초기화
void Initialize(HWND hwnd)
{
	HRESULT hr;
	// D3D11 디바이스 생성
	UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	D3D_FEATURE_LEVEL featureLevel;
	D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
	D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
		creationFlags, levels, 1,
		D3D11_SDK_VERSION, g_d3dDevice.GetAddressOf(), &featureLevel, nullptr);

	// D2D 팩토리 및 디바이스
	ComPtr<ID2D1Factory8> d2dFactory;
	D2D1_FACTORY_OPTIONS options = { D2D1_DEBUG_LEVEL_INFORMATION };
	D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, options, d2dFactory.GetAddressOf());

	ComPtr<IDXGIDevice> dxgiDevice;
	g_d3dDevice.As(&dxgiDevice);
	ComPtr<ID2D1Device7> d2dDevice;
	d2dFactory->CreateDevice((dxgiDevice.Get()), d2dDevice.GetAddressOf());
	d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, g_d2dDeviceContext.GetAddressOf());

	ComPtr<IDXGIFactory7> dxgiFactory;
	hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));
	assert(SUCCEEDED(hr));

	// SwapChain 생성
	DXGI_SWAP_CHAIN_DESC1 scDesc = {};
	scDesc.Width = g_width;
	scDesc.Height = g_height;
	scDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	scDesc.SampleDesc.Count = 1;
	scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	scDesc.BufferCount = 2;
	scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	hr = dxgiFactory->CreateSwapChainForHwnd(g_d3dDevice.Get(), hwnd, &scDesc, nullptr, nullptr, g_dxgiSwapChain.GetAddressOf());
	assert(SUCCEEDED(hr));

	// 백버퍼를 타겟으로 설정
	ComPtr<IDXGISurface> backBuffer;
	g_dxgiSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
	D2D1_BITMAP_PROPERTIES1 bmpProps = D2D1::BitmapProperties1(
		D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
		D2D1::PixelFormat(scDesc.Format, D2D1_ALPHA_MODE_PREMULTIPLIED)
	);
	g_d2dDeviceContext->CreateBitmapFromDxgiSurface(backBuffer.Get(), &bmpProps, g_d2dBitmapTarget.GetAddressOf());
	//g_d2dDeviceContext->SetTarget(g_d2dBitmapTarget.Get());


	// Create WIC factory
	hr = CoCreateInstance(CLSID_WICImagingFactory,
		NULL, CLSCTX_INPROC_SERVER,
		__uuidof(g_wicImagingFactory),
		(void**)g_wicImagingFactory.GetAddressOf());

	hr = CreateBitmapFromFile(L"../Resource/mushroom.png",g_d2dBitmapMushroom.GetAddressOf());
	assert(SUCCEEDED(hr));

	// Effect
	//1.AffineTransform2D 이펙트 생성	
	hr = g_d2dDeviceContext->CreateEffect(CLSID_D2D12DAffineTransform, g_skewEffect.GetAddressOf());

	// 반투명 검정색으로 바꾸기 위해 ColorMatrix 사용 (선택적)	
	hr = g_d2dDeviceContext->CreateEffect(CLSID_D2D1ColorMatrix, g_shadowEffect.GetAddressOf());


	D2D1_MATRIX_3X2_F skewMatrix = {
		1.0f, 0.0f,      // X scale, Y shear
		1.4f, 1.0f,      // X shear, Y scale
		-60.0f, 0.0f       // X, Y translation
	};

	// 3. 이펙트에 입력 이미지와 행렬 설정
	g_skewEffect->SetInput(0, g_d2dBitmapMushroom.Get());  // 캐릭터 비트맵
	g_skewEffect->SetValue(D2D1_2DAFFINETRANSFORM_PROP_TRANSFORM_MATRIX, skewMatrix);
	g_skewEffect->SetValue(D2D1_2DAFFINETRANSFORM_PROP_INTERPOLATION_MODE, D2D1_2DAFFINETRANSFORM_INTERPOLATION_MODE_LINEAR);
	D2D1_MATRIX_5X4_F shadowMatrix = {
		0, 0, 0, 0,  // R
		0, 0, 0, 0,  // G
		0, 0, 0, 0,  // B
		0, 0, 0, 0.5f,  // A (투명도 조절)
		0, 0, 0, 0   // Offset
	};
	g_shadowEffect->SetInputEffect(0, g_skewEffect.Get());
	g_shadowEffect->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, shadowMatrix);


	//////////////////////////////////////////////////////////////////////////
	// Screen 관련생성 Effect	
	DXGI_SURFACE_DESC scDesc1;
	D2D1_SIZE_U size;
	hr = backBuffer->GetDesc(&scDesc1); 
	assert(SUCCEEDED(hr));
	size.width = scDesc.Width;
	size.height = scDesc.Height;

	D2D1_BITMAP_PROPERTIES1 bmpSceneProps = D2D1::BitmapProperties1(
		D2D1_BITMAP_OPTIONS_TARGET,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
	);

	hr = g_d2dDeviceContext->CreateBitmap(
		size,
		nullptr,            // 초기 데이터 없음
		0,                  // pitch
		&bmpSceneProps,
		g_d2dBitmapScene.GetAddressOf()
	);
	assert(SUCCEEDED(hr));

	hr = CreateBitmapFromFile(L"../Resource/Blood.png", g_d2dBitmapBlood.GetAddressOf());
	assert(SUCCEEDED(hr));

	hr = g_d2dDeviceContext->CreateEffect(CLSID_D2D1Saturation, g_sceneEffect.GetAddressOf());
	g_sceneEffect->SetInput(0, g_d2dBitmapScene.Get());     // 원래 화면
	g_sceneEffect->SetValue(D2D1_SATURATION_PROP_SATURATION, 0.0f);


}

void Uninitialize()
{
	g_sceneEffect = nullptr;
	g_d2dBitmapScene = nullptr;
	g_d2dBitmapBlood = nullptr;
	g_d2dBitmapMushroom = nullptr;

	g_shadowEffect = nullptr;
	g_skewEffect = nullptr;

	g_wicImagingFactory = nullptr;
	g_d3dDevice = nullptr;
	g_dxgiSwapChain = nullptr;
	g_d2dDeviceContext = nullptr;
	g_d2dBitmapTarget = nullptr;
}

void Render()
{
	
	g_d2dDeviceContext->SetTarget(g_d2dBitmapScene.Get());
	g_d2dDeviceContext->BeginDraw();

	g_d2dDeviceContext->Clear(D2D1::ColorF(D2D1::ColorF::WhiteSmoke));
	//  그림자 출력
	D2D1::Matrix3x2F translation = D2D1::Matrix3x2F::Translation(600.0f, 600.0f);
	g_d2dDeviceContext->SetTransform(translation);

	g_d2dDeviceContext->DrawImage(g_shadowEffect.Get());
	g_d2dDeviceContext->DrawBitmap(g_d2dBitmapMushroom.Get());


	g_d2dDeviceContext->SetTransform(D2D1::Matrix3x2F::Identity());
	
	D2D1_SIZE_F size = g_d2dBitmapScene->GetSize();	//	그릴 크기
	D2D1_RECT_F DestRect{ 0,0,size.width,size.height};	
	g_d2dDeviceContext->DrawBitmap(
		g_d2dBitmapBlood.Get(),
		DestRect,           // g_d2dBitmapScene 크기에 맞게 늘림
		1.0f,              // Opacity (0.0 ~ 1.0)
		D2D1_INTERPOLATION_MODE_LINEAR,
		nullptr            // 이미지 원본 영역 전체 사용
	);

	g_d2dDeviceContext->EndDraw();


	g_d2dDeviceContext->SetTransform(D2D1::Matrix3x2F::Identity());
	g_d2dDeviceContext->SetTarget(g_d2dBitmapTarget.Get());
	g_d2dDeviceContext->BeginDraw();

	
	if (g_useScreenEffect)
		g_d2dDeviceContext->DrawImage(g_sceneEffect.Get());
	else
		g_d2dDeviceContext->DrawBitmap(g_d2dBitmapScene.Get());
	
	g_d2dDeviceContext->EndDraw();
	g_dxgiSwapChain->Present(1, 0);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
	

	WNDCLASS wc = {};
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = L"MyD2DWindowClass";
	RegisterClass(&wc);

	SIZE clientSize = { (LONG)g_width,(LONG)g_height };
	RECT clientRect = { 0, 0, clientSize.cx, clientSize.cy };
	AdjustWindowRect(&clientRect, WS_OVERLAPPEDWINDOW, FALSE);

	g_hwnd = CreateWindowEx(0, L"MyD2DWindowClass", L"D2D1 ScreenEffect Example",
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
		clientRect.right - clientRect.left, clientRect.bottom - clientRect.top,
		nullptr, nullptr, hInstance, nullptr);
	ShowWindow(g_hwnd, nCmdShow);


	CoInitialize(nullptr);
	Initialize(g_hwnd);
	// 메시지 루프
	MSG msg = {};
	while (msg.message != WM_QUIT) {
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else {
			Render(); 
		}
	}

	Uninitialize();
	CoUninitialize();
	return 0;
}
