#include <windows.h>
#include <d3d11.h>
#include <d2d1_3.h> //ID2D1Factory8,ID2D1DeviceContext7
#include <dxgi1_6.h> // IDXGIFactory7
#include <wrl.h>  // ComPtr 사용을 위한 헤더


#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dxgi.lib")

using namespace Microsoft::WRL;

// 전역 변수
HWND g_hwnd = nullptr;

IDXGISwapChain1* g_dxgiSwapChain = nullptr;
ID2D1DeviceContext7* g_d2dDeviceContext = nullptr;
ID2D1Bitmap1* g_d2dBitmapTarget = nullptr;

UINT g_width = 800;
UINT g_height = 600;

// 윈도우 프로시저
void InitD3DAndD2D(HWND hwnd);
void UninitD3DAndD2D();

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		break;
	}	
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

// 초기화
void InitD3DAndD2D(HWND hwnd)
{
	// D3D11 
	ID3D11Device* d3dDevice = nullptr;
	IDXGIDevice* dxgiDevice = nullptr;

	D3D_FEATURE_LEVEL featureLevel;
	D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
	D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
		D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 1,
		D3D11_SDK_VERSION, &d3dDevice, &featureLevel, nullptr);
	
	//g_d3dDevice->QueryInterface(IID_PPV_ARGS(&g_dxgiDevice));	
	d3dDevice->QueryInterface(__uuidof(dxgiDevice), (void**)&dxgiDevice);

	// D2D1	
	ID2D1Factory8* d2dFactory = nullptr;
	ID2D1Device7* d2dDevice = nullptr;

	D2D1_FACTORY_OPTIONS options = {};
	D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, options, &d2dFactory);
	d2dFactory->CreateDevice(dxgiDevice, &d2dDevice);	
	d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
		&g_d2dDeviceContext);

	// DXGI
	IDXGIFactory7* dxgiFactory = nullptr;	//
	CreateDXGIFactory(__uuidof(dxgiFactory), (void**)&dxgiFactory);

	// SwapChain 생성
	DXGI_SWAP_CHAIN_DESC1 scDesc = {};
	scDesc.Width = g_width;
	scDesc.Height = g_height;
	scDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	scDesc.SampleDesc.Count = 1;
	scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	scDesc.BufferCount = 2;
	scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	dxgiFactory->CreateSwapChainForHwnd(d3dDevice, hwnd, &scDesc, nullptr, nullptr,
		&g_dxgiSwapChain);

	// 백버퍼를 타겟으로 설정
	IDXGISurface* backBuffer=nullptr;
	g_dxgiSwapChain->GetBuffer(0, __uuidof(backBuffer), (void**)&backBuffer); 
	D2D1_BITMAP_PROPERTIES1 bmpProps = D2D1::BitmapProperties1(
		D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
		D2D1::PixelFormat(scDesc.Format, D2D1_ALPHA_MODE_PREMULTIPLIED)
	);
	g_d2dDeviceContext->CreateBitmapFromDxgiSurface(backBuffer, &bmpProps, 
		&g_d2dBitmapTarget);
	g_d2dDeviceContext->SetTarget(g_d2dBitmapTarget);	

	backBuffer->Release();
	dxgiFactory->Release();
	dxgiDevice->Release();	
	d3dDevice->Release();
	d2dFactory->Release();
	d2dDevice->Release();
}

void UninitD3DAndD2D()
{
	if (g_d2dDeviceContext) g_d2dDeviceContext->Release();
	if (g_dxgiSwapChain) g_dxgiSwapChain->Release();
	if (g_d2dBitmapTarget) g_d2dBitmapTarget->Release();
}

void Render()
{
	g_d2dDeviceContext->BeginDraw();
	g_d2dDeviceContext->Clear(D2D1::ColorF(D2D1::ColorF::DarkSlateBlue));
	g_d2dDeviceContext->EndDraw();
	g_dxgiSwapChain->Present(1, 0);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
	HRESULT hr = S_OK;
	// COM 사용 시작
	hr = CoInitialize(NULL);

	WNDCLASS wc = {};
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = L"MyD2DWindowClass";
	RegisterClass(&wc);

	SIZE clientSize = { (LONG)g_width,(LONG)g_height };
	RECT clientRect = { 0, 0, clientSize.cx, clientSize.cy };
	AdjustWindowRect(&clientRect, WS_OVERLAPPEDWINDOW, FALSE);

	g_hwnd = CreateWindowEx(0, L"MyD2DWindowClass", L"D2D1 Clear Example",
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 
		clientRect.right - clientRect.left, clientRect.bottom - clientRect.top,
		nullptr, nullptr, hInstance, nullptr);
	ShowWindow(g_hwnd, nCmdShow);

	InitD3DAndD2D(g_hwnd);

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

	UninitD3DAndD2D(); 	

	CoUninitialize();
	return 0;
}
