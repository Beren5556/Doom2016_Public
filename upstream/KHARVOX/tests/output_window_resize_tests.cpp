#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/vulkan/CoreSurfaceWindow.h"
#include <atomic>
#include <thread>
#include <stdexcept>
#include <iostream>
static std::atomic<unsigned> sizes{};
static LRESULT CALLBACK windowProcedure(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==WM_WINDOWPOSCHANGING)reinterpret_cast<WINDOWPOS*>(l)->flags|=SWP_NOSIZE;
    if(message==WM_SIZE)++sizes;
    return DefWindowProcW(window,message,w,l);
}
int main(){
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=windowProcedure;cls.lpszClassName=L"KharvoxOutputResizeTest";
    if(!RegisterClassW(&cls))return 1;
    HWND window=CreateWindowW(cls.lpszClassName,L"Output resize fixture",WS_OVERLAPPEDWINDOW,0,0,320,240,nullptr,nullptr,cls.hInstance,nullptr);
    if(!window)return 1;
    for(const auto target:{VkExtent2D{1600,1700},VkExtent2D{1700,1800},VkExtent2D{1200,1300}}){
        std::atomic<bool> requested{},finished{};const auto before=sizes.load();
        std::thread renderer([&]{requested=kharvox::requestCoreSurfaceWindow(window,target);finished=true;});
        // The renderer must return even while the owning thread is not pumping.
        for(unsigned n=0;n<100&&!finished;++n)Sleep(2);
        if(!finished)TerminateProcess(GetCurrentProcess(),2);
        renderer.join();if(!requested)return 3;
        bool matched=false;
        for(unsigned n=0;n<200&&!matched;++n){
            MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
            RECT actual{};GetClientRect(window,&actual);matched=actual.right==target.width&&actual.bottom==target.height&&sizes>before;
            if(!matched)Sleep(2);
        }
        if(!matched)return 4;
    }
    DestroyWindow(window);UnregisterClassW(cls.lpszClassName,cls.hInstance);
    std::cout<<"PASS asynchronous resize, normal WM_SIZE notifications, monitor-limit bypass and repeated sizes\n";
}
