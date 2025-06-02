#include <windows.h>
#include <assert.h>
#include <wrl.h>  // ComPtr 사용을 위한 헤더

#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")

#include <d2d1_3.h> //ID2D1Factory8,ID2D1DeviceContext7
#pragma comment(lib, "d2d1.lib")

#include <dxgi1_6.h> // IDXGIFactory7
#pragma comment(lib, "dxgi.lib")

#include <wincodec.h>
#pragma comment(lib,"windowscodecs.lib")

using namespace Microsoft::WRL;

// 전역 변수
HWND g_hwnd = nullptr;
ComPtr<ID3D11Device> g_d3dDevice;
ComPtr<IDXGISwapChain1> g_dxgiSwapChain;
ComPtr<ID2D1DeviceContext7> g_d2dDeviceContext;
ComPtr<ID2D1Bitmap1> g_d2dBitmapTarget;

// For ImageDraw
ComPtr<IWICImagingFactory> g_wicImagingFactory;
//PEG나 PNG, BMP, GIF 등 적절한 디코더(IWICBitmapDecoder)를 내부적으로 선택하고
//해당 파일을 해석(디코딩)하여 메모리상의 비트맵 프레임을 얻을 수 있게 함. 

ComPtr<ID2D1Bitmap1> g_d2dBitmapFromFile;

UINT g_width = 1024;
UINT g_height = 768;
bool g_resized = false;

void Initialize(HWND hwnd);
void Uninitialize();

// WIC를 통해 PNG 등을 로드하여 ID2D1Bitmap1**으로 반환
HRESULT CreateBitmapFromFile(const wchar_t* path, ID2D1Bitmap1** outBitmap)
{
	ComPtr<IWICBitmapDecoder>     decoder;    // 이미지 포멧 디코더. 스트림 데이터를 디코딩하여 프레임 단위의 비트맵 객체로 변환. 즉, 픽셀 정보를 추출.  
	ComPtr<IWICBitmapFrameDecode> frame;	  // 이미지를 그릴 프레임 
	ComPtr<IWICFormatConverter>   converter;  // WIC(Windows Imaging Component)에서 읽어들인 비트맵 소스를 다른 픽셀 포맷으로 변환

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
		frame.Get(),					// 어떤 비트맵(프레임)을 변환할 것인가?”를 지정하는 역할
		GUID_WICPixelFormat32bppPBGRA,  // 어떤 픽셀 포맷으로 변환할 것인지”를 지정하는 GUID
		WICBitmapDitherTypeNone,        // 픽셀 간 경계를 부드럽게 표현 여부 결정 // 블렌딩을 말하는 것. 
		nullptr,						// 어떤 색상 팔레트 기반 포맷을 사용할 것인지 지정. nullptr이면 팔레트 없음 
		0.0f,							// 알파 임계값
		WICBitmapPaletteTypeCustom	    // 원본 이미지에서 팔레트 기반 변환 시, 어떤 팔레트 전략을 쓸 것인지”를 지정 // 고정 팔레트와 동적 팔레트 전략이 존재 
	);
	// HRESULT를 반환합니다. 성공하면 S_OK 또는 S_FALSE, 실패하면 적절한 에러 코드(E_FAIL, E_INVALIDARG 등)
	if (FAILED(hr)) return hr;

	// ⑤ Direct2D 비트맵 속성 (premultiplied alpha, B8G8R8A8_UNORM)
	D2D1_BITMAP_PROPERTIES1 bmpProps = D2D1::BitmapProperties1(
		D2D1_BITMAP_OPTIONS_NONE,// 비트맵 옵션. GDI 와 함께 쓸 때에는 D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE 사용 
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
	case WM_EXITSIZEMOVE:
		if (g_resized)
		{
			Uninitialize();
			Initialize(hWnd);
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
	D3D_FEATURE_LEVEL featureLevel;
	D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
	D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
		D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 1,
		D3D11_SDK_VERSION, g_d3dDevice.GetAddressOf(), &featureLevel, nullptr);

	// D2D 팩토리 및 디바이스
	ComPtr<ID2D1Factory8> d2dFactory;
	D2D1_FACTORY_OPTIONS options = {};
	D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, options, d2dFactory.GetAddressOf());

	ComPtr<IDXGIDevice> dxgiDevice;
	g_d3dDevice.As(&dxgiDevice);
	ComPtr<ID2D1Device7> d2dDevice;

	d2dFactory->CreateDevice((dxgiDevice.Get()), d2dDevice.GetAddressOf());
	d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, g_d2dDeviceContext.GetAddressOf());

	/// <summary>
	/// 여기서부터가 SwapChain 클래스에서 가져가야 할 부분 
	/// </summary>
	/// <param name="hwnd"></param>
	ComPtr<IDXGIFactory7> dxgiFactory;
	CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));


	// SwapChain 생성 // 더블 버퍼링. 두개의 버퍼를 체인처럼 연결해서 사용할 수 있게 하는 것. 
	DXGI_SWAP_CHAIN_DESC1 scDesc = {};
	scDesc.Width = g_width;								  // 버퍼 너비. 가로 길이 
	scDesc.Height = g_height;							  // 버퍼 높이. 세로 길이 
	scDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;			  // 각 픽셀 포맷 
	scDesc.SampleDesc.Count = 1;						  // 멀티 샘플링 사용 여부 (1 =  사용안함)  멀티 샘플링 : 가장자리가 부드러워지느냐.   
	scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 그릴 버퍼로 사용한다는 명시적 코드 
	scDesc.BufferCount = 2;								  // 더블 버퍼링을 위해 2개의 버퍼를 사용 
	scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;	  // 버퍼를 교체하는 방식 
	dxgiFactory->CreateSwapChainForHwnd(g_d3dDevice.Get(), hwnd, &scDesc, nullptr, nullptr, g_dxgiSwapChain.GetAddressOf()); // 특정 윈도우 창에 대해서 스왑 체인을 생성 하는 것 
	// 즉, 어느 윈도우 창에 있는 디바이스에 연결할 지 정하고, 
	// 버퍼 크기와 같은 정보를 읽어와서, 
	// 스크린 모드(풀 스크린 혹은 윈도우 스크린 모드)로 설정해, 
	// 출력할 화면을 정해서 
	// 생성된 스왑 체인을 골라서 출력해줄 수 있게 하는 것.


	// 백버퍼를 타겟으로 설정
	ComPtr<IDXGISurface> backBuffer;
	g_dxgiSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)); // 사용할 백버퍼를 스왑 체인에서 IDXGISurface 타입으로 가져온다. 
															  // IDXGISurface는 “DXGI 표준 Surface 인터페이스”로, GPU 상의 텍스처(또는 렌더 타겟)를 COM 인터페이스 형태로 다룰 때 사용
															  // Direct2D가 바로 그 위에 그릴 수 있도록 연결되는 다리 역할을 한다. 
	                                                          // 즉, 백버퍼를 가져와서 Direct2D의 이미지가 그려질 수 있는 Surface 로 전환 하는 것. 
	/// <summary>
	/// 여기까지가 SwapChin 클래스에서 가져가야 할 부분 
	/// </summary>

	/// <summary>
	/// 여기서부터가 이미지 매니저가 가져가야 하는 부분 
	/// </summary>
	/// <param name="hwnd"></param>
	D2D1_BITMAP_PROPERTIES1 bmpProps = D2D1::BitmapProperties1(
		D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
		// D2D1_BITMAP_OPTIONS_TARGET : “렌더 타겟(즉, 그려지는 대상)”으로 사용하겠다는 의미입니다. Direct2D의 ID2D1DeviceContext::SetTarget(...)에 바로 연결
		// D2D1_BITMAP_OPTIONS_CANNOT_DRAW : 이 비트맵을 Direct2D에서 직접 DrawBitmap(...) 같은 함수로 그리지 않고, 오직 렌더 타겟으로만 사용하겠다”는 뜻. 즉, 화면 출력용으로만 사용 
		D2D1::PixelFormat(scDesc.Format, D2D1_ALPHA_MODE_PREMULTIPLIED) 
		// scDesc.Format :  Swap Chain을 만들 때 DXGI_FORMAT_B8G8R8A8_UNORM 으로 설정했던 값을 받아와 “B8G8R8A8(32비트) 언노멀라이즈드” 포맷으로 Direct2D가 비트맵을 해석
	    // D2D1_ALPHA_MODE_PREMULTIPLIED : 알파(투명도) 채널이 곱해진 형태(“프리멀티플라이드 알파”)로 저장된 데이터를 쓴다는 의미
	); 
	// “어떤 픽셀 포맷으로, 어떤 옵션을 가진 비트맵”인지 알려주기 위해, Direct2D의 비트맵 속성을 설정하고, 구조체 D2D1_BITMAP_PROPERTIES1 에 담는다. 


	g_d2dDeviceContext->CreateBitmapFromDxgiSurface(backBuffer.Get(), &bmpProps, g_d2dBitmapTarget.GetAddressOf()); // “IDXGISurface를 바로 Direct2D의 ID2D1Bitmap1 객체로 변환(Wrap)해 달라”는 명령
	// 직접 GPU 메모리의 텍스처를 복사하는 과정 없이, 동일한 메모리를 공용(shared)으로 사용한다. 

	g_d2dDeviceContext->SetTarget(g_d2dBitmapTarget.Get());
		//Direct2D의 장치 컨텍스트(ID2D1DeviceContext)가 “이제부터 그릴 대상(target)을 방금 래핑한 비트맵으로 삼겠다”

	// Create WIC factory
	hr = CoCreateInstance(CLSID_WICImagingFactory,
			NULL,CLSCTX_INPROC_SERVER,
			__uuidof(g_wicImagingFactory),
			(void**)g_wicImagingFactory.GetAddressOf());
	//이미지 파일(PNG 등)을 로드하여 ID2D1Bitmap1 객체로 만드는 초기화 과정

	hr = CreateBitmapFromFile(L"../Resource/mushroom.png", g_d2dBitmapFromFile.GetAddressOf());
	assert(SUCCEEDED(hr));
	/// <summary>
	/// 여기까지가 이미지 매니저가 가져가야 하는 부분 
	/// </summary>
	/// <param name="hwnd"></param>
}

void Uninitialize()
{
	g_wicImagingFactory = nullptr;
	g_d2dBitmapFromFile = nullptr;

	g_d3dDevice = nullptr;
	g_dxgiSwapChain = nullptr;
	g_d2dDeviceContext = nullptr;
	g_d2dBitmapTarget = nullptr;
}

void Render()
{
	g_d2dDeviceContext->BeginDraw();
	g_d2dDeviceContext->Clear(D2D1::ColorF(D2D1::ColorF::DarkSlateBlue));

	D2D1_SIZE_F size;

	g_d2dDeviceContext->SetTransform(D2D1::Matrix3x2F::Identity()); // 변환 초기화

	//1. 0,0 위치에 비트맵 전체영역 그린다. (변환은 초기화)
	g_d2dDeviceContext->DrawBitmap(g_d2dBitmapFromFile.Get());

	//2. DestPos(화면 위치) 설정과 SrcPos(비트맵 위치)로 그리기
	D2D1_VECTOR_2F DestPos{ 0,0 }, SrcPos{ 0,0 }; // 화면 위치, 비트맵 위치
	size = { 0,0 };	//	그릴 크기
	D2D1_RECT_F DestRect{ 0,0,0,0 }, SrcRect{ 0,0,0,0 }; // 화면 영역, 비트맵 영역
	D2D1_MATRIX_3X2_F transform;	// 변환 행렬

	size = g_d2dBitmapFromFile->GetSize();
	DestPos = { 100,0 };
	DestRect = { DestPos.x , DestPos.y, DestPos.x + size.width - 1 ,DestPos.y + size.height - 1 };
	g_d2dDeviceContext->DrawBitmap(g_d2dBitmapFromFile.Get(), DestRect);


	//3. DestRect(그릴 영역) 설정과 SrcRect(비트맵 일부 영역)로 그리기
	size = { 200,200 };
	DestPos = { 100,100 };
	DestRect = { DestPos.x , DestPos.y, DestPos.x + size.width - 1 ,DestPos.y + size.height - 1 };

	SrcPos = { 0,0 };
	SrcRect = { SrcPos.x,SrcPos.y, SrcPos.x + size.width - 1 ,SrcPos.y + size.height - 1 };
	g_d2dDeviceContext->DrawBitmap(g_d2dBitmapFromFile.Get(), DestRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &SrcRect);


	//4. 변환을 사용한 반전으로 DestRect(그릴 영역) 설정과 SrcRect(비트맵 일부 영역)로 그리기
	DestPos = { 700,100 };
	DestRect = { DestPos.x , DestPos.y, DestPos.x + size.width - 1 ,DestPos.y + size.height - 1 };

	transform = D2D1::Matrix3x2F::Scale(-1.0f, 1.0f,  // x축 반전
		D2D1::Point2F(DestPos.x, DestPos.y));        // 기준점
	g_d2dDeviceContext->SetTransform(transform);
	g_d2dDeviceContext->DrawBitmap(g_d2dBitmapFromFile.Get(), DestRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &SrcRect);

	//5. 복합변환을 사용한 반전으로 DestRect(그릴 영역) 설정과 SrcRect(비트맵 일부 영역)로 그리기
	DestPos = { 0,0 };   // 그릴 위치는 0,0으로 하고 이동변환을 사용한다.
	DestRect = { DestPos.x , DestPos.y, DestPos.x + size.width - 1 ,DestPos.y + size.height - 1 };

	transform = D2D1::Matrix3x2F::Scale(1.0f, 1.0f, D2D1::Point2F(0.0f, 0.0f)) *
		D2D1::Matrix3x2F::Rotation(90.0f, D2D1::Point2F(0.0f, 0.0f)) * // 90도 회전
		D2D1::Matrix3x2F::Translation(900.0f, 900.0f);  // 이동

	// 기준점
	g_d2dDeviceContext->SetTransform(transform);
	g_d2dDeviceContext->DrawBitmap(g_d2dBitmapFromFile.Get(), DestRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &SrcRect);

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

	g_hwnd = CreateWindowEx(0, L"MyD2DWindowClass", L"D2D1 Clear Example",
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
