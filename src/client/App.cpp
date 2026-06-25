// src/client/App.cpp
#define NOMINMAX
// Full Vulkan + ImGui rendering with swapchain, themes, toasts, avatars, role-based UI
#include "App.h"
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "imgui_impl_glfw.h"
#include "Chat.h"
#include "Auth.h"
#include "Roles.h"
#include "Voice.h"
#include "Utils.h"
#include "logo_data.h"
#include "avatar_data.h"
#include <fstream>

static void SavePrefs(const std::string& user, const std::string& pass, bool remember) {
    try {
        nlohmann::json j;
        j["username"] = user;
        j["password"] = pass;
        j["remember_me"] = remember;
        std::ofstream f("client_prefs.json");
        if (f.is_open()) {
            f << j.dump(4);
        }
    } catch (...) {}
}

static void LoadPrefs(char* user, size_t userSize, char* pass, size_t passSize, bool& remember) {
    try {
        std::ifstream f("client_prefs.json");
        if (f.is_open()) {
            nlohmann::json j;
            f >> j;
            remember = j.value("remember_me", false);
            if (remember) {
                std::string u = j.value("username", "");
                std::string p = j.value("password", "");
                strncpy(user, u.c_str(), userSize - 1);
                strncpy(pass, p.c_str(), passSize - 1);
            }
        }
    } catch (...) {}
}
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <cstdlib>

// ================================================================
//  Vulkan global state
// ================================================================
static VkInstance        g_Instance    = VK_NULL_HANDLE;
static VkPhysicalDevice  g_PhysDev     = VK_NULL_HANDLE;
static VkDevice          g_Device      = VK_NULL_HANDLE;
static VkQueue           g_Queue       = VK_NULL_HANDLE;
static uint32_t          g_QueueFamily = 0;
static VkSurfaceKHR      g_Surface     = VK_NULL_HANDLE;
static VkRenderPass      g_RenderPass  = VK_NULL_HANDLE;
static VkDescriptorPool  g_DescPool    = VK_NULL_HANDLE;
static VkCommandPool     g_CmdPool     = VK_NULL_HANDLE;
static VkSwapchainKHR    g_Swapchain   = VK_NULL_HANDLE;
static VkExtent2D        g_SwapExtent  = {};
static VkFormat          g_SwapFormat  = VK_FORMAT_B8G8R8A8_UNORM;

static std::vector<VkImage>         g_SwapImages;
static std::vector<VkImageView>     g_SwapViews;
static std::vector<VkFramebuffer>   g_Framebuffers;
static std::vector<VkCommandBuffer> g_CmdBuffers;

static VkSemaphore g_SemImageAvail = VK_NULL_HANDLE;
static VkSemaphore g_SemRenderDone = VK_NULL_HANDLE;
static VkFence     g_FenceInFlight = VK_NULL_HANDLE;

// ================================================================
//  UI global state
// ================================================================
static bool g_DarkMode = true;

#ifdef _WIN32
#include <windows.h>
#include <vector>
#include <cstdint>

// Captures the primary screen, downscales to 480x270, and converts to RGB565.
// Returns a vector of uint16_t (size 480 * 270 = 129,600 elements).
std::vector<uint16_t> CaptureScreen565() {
    std::vector<uint16_t> buffer(480 * 270, 0);

    HWND hwnd = GetDesktopWindow();
    HDC hdcScreen = GetDC(hwnd);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    // Create a 32-bit DIB section for easy pixel access of downscaled image
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = 480;
    bmi.bmiHeader.biHeight = -270; // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pPixels = nullptr;
    HBITMAP hbmpMem = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pPixels, NULL, 0);
    if (!hbmpMem) {
        DeleteDC(hdcMem);
        ReleaseDC(hwnd, hdcScreen);
        return buffer;
    }

    HGDIOBJ hOld = SelectObject(hdcMem, hbmpMem);

    // Stretch block transfer (downscale)
    SetStretchBltMode(hdcMem, COLORONCOLOR);
    StretchBlt(hdcMem, 0, 0, 480, 270, hdcScreen, 0, 0, screenWidth, screenHeight, SRCCOPY);

    // Convert 32-bit BGRA to 16-bit RGB565
    if (pPixels) {
        const uint32_t* src = reinterpret_cast<const uint32_t*>(pPixels);
        for (int i = 0; i < 480 * 270; ++i) {
            uint32_t pixel = src[i];
            uint8_t r = (pixel >> 16) & 0xFF;
            uint8_t g = (pixel >> 8) & 0xFF;
            uint8_t b = pixel & 0xFF;

            buffer[i] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        }
    }

    SelectObject(hdcMem, hOld);
    DeleteObject(hbmpMem);
    DeleteDC(hdcMem);
    ReleaseDC(hwnd, hdcScreen);

    return buffer;
}
#else
std::vector<uint16_t> CaptureScreen565() {
    return std::vector<uint16_t>(480 * 270, 0); // Mock for non-windows (though user is on Windows)
}
#endif

struct DynamicVulkanTexture {
    VkImage Image = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkImageView View = VK_NULL_HANDLE;
    VkSampler Sampler = VK_NULL_HANDLE;
    VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;

    int Width = 0;
    int Height = 0;

    void Create(int w, int h) {
        Width = w;
        Height = h;

        // 1. Create Image
        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.extent.width = w;
        imageInfo.extent.height = h;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_LINEAR; // Linear makes it easy to map and write pixels directly!
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;

        if (vkCreateImage(g_Device, &imageInfo, nullptr, &Image) != VK_SUCCESS) {
            std::cerr << "Failed to create Vulkan screen share image" << std::endl;
            return;
        }

        // Memory requirements
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(g_Device, Image, &req);

        // Find memory type
        VkPhysicalDeviceMemoryProperties memProp;
        vkGetPhysicalDeviceMemoryProperties(g_PhysDev, &memProp);
        uint32_t memTypeIndex = UINT32_MAX;
        for (uint32_t i = 0; i < memProp.memoryTypeCount; ++i) {
            if ((req.memoryTypeBits & (1 << i)) && 
                (memProp.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
                memTypeIndex = i;
                break;
            }
        }

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = req.size;
        allocInfo.memoryTypeIndex = memTypeIndex;

        vkAllocateMemory(g_Device, &allocInfo, nullptr, &Memory);
        vkBindImageMemory(g_Device, Image, Memory, 0);

        // 2. Create View
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = Image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        vkCreateImageView(g_Device, &viewInfo, nullptr, &View);

        // 3. Create Sampler
        VkSamplerCreateInfo samplerInfo = {};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        vkCreateSampler(g_Device, &samplerInfo, nullptr, &Sampler);

        // 4. Create ImGui DescriptorSet
        DescriptorSet = ImGui_ImplVulkan_AddTexture(Sampler, View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    void Update(const uint8_t* rgbaPixels) {
        if (!rgbaPixels || Image == VK_NULL_HANDLE || Memory == VK_NULL_HANDLE) return;

        // Map and write
        void* data = nullptr;
        VkImageSubresource sub = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
        VkSubresourceLayout layout;
        vkGetImageSubresourceLayout(g_Device, Image, &sub, &layout);

        vkMapMemory(g_Device, Memory, 0, layout.size, 0, &data);
        if (data) {
            uint8_t* dst = reinterpret_cast<uint8_t*>(data);
            if (layout.rowPitch == Width * 4) {
                memcpy(dst, rgbaPixels, Width * Height * 4);
            } else {
                for (int y = 0; y < Height; ++y) {
                    memcpy(dst + y * layout.rowPitch, rgbaPixels + y * Width * 4, Width * 4);
                }
            }
            vkUnmapMemory(g_Device, Memory);
        }
    }

    void Destroy() {
        if (g_Device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(g_Device);
            if (Sampler) vkDestroySampler(g_Device, Sampler, nullptr);
            if (View) vkDestroyImageView(g_Device, View, nullptr);
            if (Image) vkDestroyImage(g_Device, Image, nullptr);
            if (Memory) vkFreeMemory(g_Device, Memory, nullptr);
        }
        Image = VK_NULL_HANDLE;
        Memory = VK_NULL_HANDLE;
        View = VK_NULL_HANDLE;
        Sampler = VK_NULL_HANDLE;
        DescriptorSet = VK_NULL_HANDLE;
    }
};

static DynamicVulkanTexture g_ScreenShareTexture;
static DynamicVulkanTexture g_LogoTexture;
static DynamicVulkanTexture g_AvatarTextures[AVATAR_COUNT];

enum class ActiveTab { CHAT, DMS, VOICE, MODERATION, SETTINGS };
static ActiveTab g_ActiveTab = ActiveTab::CHAT;
static bool g_ShowUserList = true; // Toggle user list inside Chat
static int64_t g_ReplyToId = 0;
static std::string g_ReplyToSender = "";
static std::string g_SelectedDMUser = "";
static bool g_RememberMe = false;
static bool g_WasLoggedIn = false;
static bool g_AmITyping = false;
static float g_MyTypingTimer = 0.f;


// ---- Toast system ----
enum class ToastType { INFO, SUCCESS, WARNING, ERR };
struct Toast {
    std::string message;
    ToastType   type;
    float       timeLeft; // seconds remaining
    float       duration; // total duration (for alpha calc)
};
static std::vector<Toast> g_Toasts;

void AddToast(const std::string& msg, ToastType type = ToastType::INFO, float dur = 3.0f) {
    // Avoid duplicate toasts
    for (auto& t : g_Toasts)
        if (t.message == msg) { t.timeLeft = dur; return; }
    g_Toasts.push_back({ msg, type, dur, dur });
}

// ================================================================
//  Forward declarations
// ================================================================
bool SetupVulkan(GLFWwindow* window);
bool CreateSwapchain(int w, int h);
void DestroySwapchain();
void CleanupVulkan();
void ApplyDarkTheme();
void ApplyLightTheme();
void DrawAvatar(ImVec2 center, float radius, const std::string& username, const std::string& role, int avatarId = -1);
void RenderToasts(const ImGuiIO& io, float dt);
void RenderDashboard(const ImGuiIO& io, char* messageBuffer, char* serverIp, float dt);
std::string FormatTimestamp(int64_t ts);

// ================================================================
//  RunClientApp
// ================================================================
int RunClientApp()
{
    if (!ClientChat::GetInstance().Init()) {
        std::cerr << "ENet init failed" << std::endl; return -1;
    }

    if (!glfwInit()) { std::cerr << "GLFW init failed" << std::endl; return -1; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "RBG TEAM Yonetim Paneli", nullptr, nullptr);
    if (!window) { std::cerr << "Window creation failed" << std::endl; return -1; }

    // Set window icon
    {
        GLFWimage images[1];
        images[0].width = LOGO_WIDTH;
        images[0].height = LOGO_HEIGHT;
        images[0].pixels = const_cast<unsigned char*>(LOGO_RGBA);
        glfwSetWindowIcon(window, 1, images);
    }

    if (!SetupVulkan(window)) { std::cerr << "Vulkan setup failed" << std::endl; return -1; }

    int fw, fh;
    glfwGetFramebufferSize(window, &fw, &fh);
    if (!CreateSwapchain(fw, fh)) { std::cerr << "Swapchain failed" << std::endl; return -1; }
    std::cout << "Swapchain: " << fw << "x" << fh << " (" << g_SwapImages.size() << " images)" << std::endl;

    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    float dpiScale = (xscale > yscale) ? xscale : yscale;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.IniFilename = nullptr; // Don't save imgui.ini

    // Load smooth vector system font with DPI scaling and Turkish character support
    float fontSize = 17.0f * dpiScale;
    
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    const ImWchar custom_ranges[] = {
        0x0100, 0x017F, // Latin Extended-A (includes Turkish characters: Ğ, ğ, İ, ı, Ş, ş)
        0x2600, 0x27BF, // Dingbats & Miscellaneous Symbols (covers stars, hearts, skull, crossed swords, biohazard, pencil, etc.)
        0
    };
    builder.AddRanges(custom_ranges);
    static ImVector<ImWchar> ranges;
    ranges.clear();
    builder.BuildRanges(&ranges);

    std::string winDir = "C:\\Windows";
    if (const char* sysRoot = getenv("SystemRoot")) {
        winDir = sysRoot;
    } else if (const char* windirEnv = getenv("windir")) {
        winDir = windirEnv;
    }

    std::string segoePath = winDir + "\\Fonts\\segoeui.ttf";
    std::string arialPath = winDir + "\\Fonts\\arial.ttf";

    ImFont* font = io.Fonts->AddFontFromFileTTF(segoePath.c_str(), fontSize, nullptr, ranges.Data);
    if (font) {
        std::cout << "[UI] Loaded font: " << segoePath << std::endl;
    } else {
        font = io.Fonts->AddFontFromFileTTF(arialPath.c_str(), fontSize, nullptr, ranges.Data);
        if (font) {
            std::cout << "[UI] Loaded fallback font: " << arialPath << std::endl;
        } else {
            std::cerr << "[UI] Warning: Failed to load Segoe UI or Arial from Windows Fonts. Falling back to default font." << std::endl;
            io.Fonts->AddFontDefault();
        }
    }

    ApplyDarkTheme();
    ImGui::GetStyle().ScaleAllSizes(dpiScale);

    ImGui_ImplGlfw_InitForVulkan(window, true);
    ImGui_ImplVulkan_InitInfo ii = {};
    ii.Instance       = g_Instance;
    ii.PhysicalDevice = g_PhysDev;
    ii.Device         = g_Device;
    ii.QueueFamily    = g_QueueFamily;
    ii.Queue          = g_Queue;
    ii.DescriptorPool = g_DescPool;
    ii.MinImageCount  = 2;
    ii.ImageCount     = (uint32_t)g_SwapImages.size();
    ImGui_ImplVulkan_Init(&ii, g_RenderPass);

    // Upload fonts
    {
        VkCommandBuffer cmd;
        VkCommandBufferAllocateInfo ai = {};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = g_CmdPool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
        vkAllocateCommandBuffers(g_Device, &ai, &cmd);
        VkCommandBufferBeginInfo bi = {}; bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);
        ImGui_ImplVulkan_CreateFontsTexture(cmd);
        vkEndCommandBuffer(cmd);
        VkSubmitInfo si = {}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
        vkQueueSubmit(g_Queue, 1, &si, VK_NULL_HANDLE);
        vkDeviceWaitIdle(g_Device);
        ImGui_ImplVulkan_DestroyFontUploadObjects();
        vkFreeCommandBuffers(g_Device, g_CmdPool, 1, &cmd);
        std::cout << "ImGui fonts uploaded" << std::endl;
    }

    // Load embedded logo texture
    g_LogoTexture.Create(LOGO_WIDTH, LOGO_HEIGHT);
    g_LogoTexture.Update(LOGO_RGBA);

    // Initialize avatar textures
    for (int i = 0; i < AVATAR_COUNT; ++i) {
        g_AvatarTextures[i].Create(AVATAR_WIDTH, AVATAR_HEIGHT);
        g_AvatarTextures[i].Update(AVATARS_RGBA[i]);
    }

    // UI state
    char serverIp[64]       = "147.185.221.180:40632";
    char username[64]       = "";
    char password[64]       = "";
    char messageBuffer[256] = "";
    bool voiceConnected     = false;

    // Load saved credentials if remember me was enabled
    LoadPrefs(username, sizeof(username), password, sizeof(password), g_RememberMe);

    auto lastTime = std::chrono::steady_clock::now();

    // ---- Main loop ----
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        glfwGetFramebufferSize(window, &fw, &fh);
        if (fw == 0 || fh == 0) continue;

        // Delta time for toasts
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        // Networking
        ClientChat::GetInstance().Poll(0);
        if (ClientAuth::GetInstance().IsLoggedIn()) {
            if (!g_WasLoggedIn) {
                g_WasLoggedIn = true;
                if (g_RememberMe) {
                    SavePrefs(username, password, true);
                } else {
                    SavePrefs("", "", false);
                }
            }

            if (!voiceConnected) {
                ClientVoice::GetInstance().Init();
                ClientVoice::GetInstance().Register(ClientAuth::GetInstance().GetUsername());
                voiceConnected = true;
                AddToast("Ses sistemi hazir!", ToastType::SUCCESS);
            }

            // Screen share sender (5 FPS)
            if (ClientVoice::GetInstance().IsScreenSharing()) {
                static float screenShareTimer = 0.f;
                screenShareTimer += dt;
                if (screenShareTimer >= 0.2f) {
                    screenShareTimer = 0.f;
                    std::vector<uint16_t> frame565 = CaptureScreen565();
                    ClientVoice::GetInstance().SendScreenFrame(frame565.data(), frame565.size() * sizeof(uint16_t));
                }
            }

            // Screen share receiver texture upload
            if (ClientVoice::GetInstance().IsReceivingScreen()) {
                std::vector<uint8_t> rgbaFrame;
                if (ClientVoice::GetInstance().GetNewFrame(rgbaFrame)) {
                    if (g_ScreenShareTexture.Image == VK_NULL_HANDLE) {
                        g_ScreenShareTexture.Create(480, 270);
                    }
                    g_ScreenShareTexture.Update(rgbaFrame.data());
                }
            } else {
                if (g_ScreenShareTexture.Image != VK_NULL_HANDLE) {
                    g_ScreenShareTexture.Destroy();
                }
            }

        } else {
            // Reset remember me login tracking flag
            // (static inside if block will not be reset directly, so let's use a flag, but since static wasLoggedIn is local to the other branch, let's declare a file-scope static or just use a helper flag, or change the static to a static file-level flag. Let's make it a file-level static by removing the local static and declaring it at file scope instead.)
            // Wait, let's just make it a global static variable instead to make it easy to modify!
            // Let's modify this chunk to use a global static variable which is much cleaner.
            // Oh, let's declare `static bool g_WasLoggedIn = false;` in the globals.
            // That is much better! Let's update this chunk to set `g_WasLoggedIn = false;` when not logged in.
            // And in the true block we check `if (!g_WasLoggedIn) { g_WasLoggedIn = true; ... }`
            g_WasLoggedIn = false;

            if (voiceConnected) {
                ClientVoice::GetInstance().Close();
                voiceConnected = false;
            }
            if (g_ScreenShareTexture.Image != VK_NULL_HANDLE) {
                g_ScreenShareTexture.Destroy();
            }
        }

        // Acquire swapchain image
        vkWaitForFences(g_Device, 1, &g_FenceInFlight, VK_TRUE, UINT64_MAX);
        uint32_t imageIndex;
        VkResult acq = vkAcquireNextImageKHR(g_Device, g_Swapchain, UINT64_MAX,
                                              g_SemImageAvail, VK_NULL_HANDLE, &imageIndex);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR || acq == VK_SUBOPTIMAL_KHR) {
            vkDeviceWaitIdle(g_Device); DestroySwapchain();
            glfwGetFramebufferSize(window, &fw, &fh); CreateSwapchain(fw, fh); continue;
        }
        vkResetFences(g_Device, 1, &g_FenceInFlight);

        // Record commands
        VkCommandBuffer cmd = g_CmdBuffers[imageIndex];
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo cbi = {}; cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &cbi);

        ImVec4 clearCol = g_DarkMode ? ImVec4(0.08f,0.08f,0.10f,1.f) : ImVec4(0.92f,0.92f,0.94f,1.f);
        VkClearValue cv = {{{clearCol.x, clearCol.y, clearCol.z, 1.0f}}};
        VkRenderPassBeginInfo rp = {};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass        = g_RenderPass;
        rp.framebuffer       = g_Framebuffers[imageIndex];
        rp.renderArea.extent = g_SwapExtent;
        rp.clearValueCount   = 1; rp.pClearValues = &cv;
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

        // ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();



        // ============================================================
        //  LOGIN SCREEN
        // ============================================================
        if (!ClientAuth::GetInstance().IsLoggedIn()) {
            ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x*0.5f, io.DisplaySize.y*0.5f),
                                    ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(440, 560), ImGuiCond_Always); // Increased height to fit logo, fields, remember me
            
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.f, 28.f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.f, 8.f));
            
            ImGui::Begin("##Login", nullptr,
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

            // Logo Image
            if (g_LogoTexture.DescriptorSet != VK_NULL_HANDLE) {
                ImGui::SetCursorPosX((440 - 80.f) * 0.5f);
                ImGui::Image((ImTextureID)g_LogoTexture.DescriptorSet, ImVec2(80.f, 80.f));
                ImGui::Dummy(ImVec2(0, 4));
            }

            // Logo / Title (Premium look)
            float textW = ImGui::CalcTextSize("RBG TEAM").x;
            ImGui::SetCursorPosX((440 - textW) * 0.5f);
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.0f, 1.0f), "RBG TEAM");
            
            textW = ImGui::CalcTextSize("Yönetim Paneli Girişi").x;
            ImGui::SetCursorPosX((440 - textW) * 0.5f);
            ImGui::TextDisabled("Yonetim Paneli Girisi");
            
            ImGui::Dummy(ImVec2(0, 10));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 10));

            static bool showRegister = false;
            static char regUsername[64] = "";
            static char regPassword[64] = "";
            static char regConfirmPassword[64] = "";

            if (!showRegister) {
                // Username input
                ImGui::Text("Kullanici Adi:");
                ImGui::Dummy(ImVec2(0, 2));
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##usr", username, sizeof(username));
                ImGui::Dummy(ImVec2(0, 8));

                // Password input
                ImGui::Text("Sifre:");
                ImGui::SameLine(ImGui::GetWindowWidth() - 95.f);
                static bool showPwd = false;
                ImGui::Checkbox("Goster##pwd_chk", &showPwd);
                ImGui::Dummy(ImVec2(0, 2));
                ImGui::SetNextItemWidth(-1);
                ImGuiInputTextFlags pwdFlags = ImGuiInputTextFlags_EnterReturnsTrue;
                if (!showPwd) pwdFlags |= ImGuiInputTextFlags_Password;
                bool enterPressed = ImGui::InputText("##pwd", password, sizeof(password), pwdFlags);
                ImGui::Dummy(ImVec2(0, 6));

                // Remember me checkbox
                ImGui::Checkbox("Beni Hatirla", &g_RememberMe);
                ImGui::Dummy(ImVec2(0, 10));

                bool doLogin = enterPressed;
                
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.42f, 0.78f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.52f, 0.90f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14f, 0.35f, 0.68f, 1.0f));
                
                if (ClientChat::GetInstance().IsConnected()) {
                    if (ImGui::Button("Giris Yap", ImVec2(-1, 38)) || doLogin) {
                        ClientChat::GetInstance().SendLogin(username, password);
                        AddToast("Giris yapiliyor...", ToastType::INFO);
                    }
                } else {
                    if (ImGui::Button("Giris Yap", ImVec2(-1, 38)) || doLogin) {
                        // Parse host:port format
                        std::string ipStr(serverIp);
                        std::string host = ipStr;
                        int port = 7777;
                        size_t colonPos = ipStr.rfind(':');
                        if (colonPos != std::string::npos) {
                            host = ipStr.substr(0, colonPos);
                            try { port = std::stoi(ipStr.substr(colonPos + 1)); }
                            catch (...) { port = 7777; }
                        }
                        if (ClientChat::GetInstance().Connect(host, port))
                            AddToast("Sunucuya baglaniliyor...", ToastType::INFO);
                        else
                            AddToast("Baglanti basarisiz!", ToastType::ERR);
                    }
                }
                ImGui::PopStyleColor(3);

                ImGui::Dummy(ImVec2(0, 4));
                if (ImGui::Button("Yeni Hesap Olustur", ImVec2(-1, 30))) {
                    showRegister = true;
                    ClientChat::GetInstance().ClearRegisterResponse();
                    memset(regUsername, 0, sizeof(regUsername));
                    memset(regPassword, 0, sizeof(regPassword));
                    memset(regConfirmPassword, 0, sizeof(regConfirmPassword));
                }
            } else {
                ImGui::Text("Kullanici Adi:");
                ImGui::Dummy(ImVec2(0, 2));
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##regusr", regUsername, sizeof(regUsername));
                ImGui::Dummy(ImVec2(0, 8));

                ImGui::Text("Sifre:");
                ImGui::SameLine(ImGui::GetWindowWidth() - 95.f);
                static bool showRegPwd = false;
                ImGui::Checkbox("Goster##reg_chk", &showRegPwd);
                ImGui::Dummy(ImVec2(0, 2));
                ImGui::SetNextItemWidth(-1);
                ImGuiInputTextFlags regPwdFlags = 0;
                if (!showRegPwd) regPwdFlags |= ImGuiInputTextFlags_Password;
                ImGui::InputText("##regpwd", regPassword, sizeof(regPassword), regPwdFlags);
                ImGui::Dummy(ImVec2(0, 8));

                ImGui::Text("Sifre Tekrar:");
                ImGui::Dummy(ImVec2(0, 2));
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##regconfirm", regConfirmPassword, sizeof(regConfirmPassword), regPwdFlags);
                ImGui::Dummy(ImVec2(0, 16));

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.6f, 0.38f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.7f, 0.45f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14f, 0.5f, 0.32f, 1.0f));

                if (ClientChat::GetInstance().IsConnected()) {
                    if (ImGui::Button("Kayit Ol", ImVec2(-1, 38))) {
                        if (strlen(regUsername) == 0 || strlen(regPassword) == 0) {
                            AddToast("Kullanici adi ve sifre bos olamaz!", ToastType::ERR);
                        } else if (strcmp(regPassword, regConfirmPassword) != 0) {
                            AddToast("Sifreler uyusmuyor!", ToastType::ERR);
                        } else {
                            ClientChat::GetInstance().SendRegister(regUsername, regPassword);
                            AddToast("Kayit talebi gonderiliyor...", ToastType::INFO);
                        }
                    }
                } else {
                    if (ImGui::Button("Kayit Ol", ImVec2(-1, 38))) {
                        if (strlen(regUsername) == 0 || strlen(regPassword) == 0) {
                            AddToast("Kullanici adi ve sifre bos olamaz!", ToastType::ERR);
                        } else if (strcmp(regPassword, regConfirmPassword) != 0) {
                            AddToast("Sifreler uyusmuyor!", ToastType::ERR);
                        } else {
                            // Connect first
                            std::string ipStr(serverIp);
                            std::string host = ipStr;
                            int port = 7777;
                            size_t colonPos = ipStr.rfind(':');
                            if (colonPos != std::string::npos) {
                                host = ipStr.substr(0, colonPos);
                                try { port = std::stoi(ipStr.substr(colonPos + 1)); }
                                catch (...) { port = 7777; }
                            }
                            if (ClientChat::GetInstance().Connect(host, port))
                                AddToast("Sunucuya baglaniliyor...", ToastType::INFO);
                            else
                                AddToast("Baglanti basarisiz!", ToastType::ERR);
                        }
                    }
                }
                ImGui::PopStyleColor(3);

                ImGui::Dummy(ImVec2(0, 4));
                if (ImGui::Button("Giris Ekranina Don", ImVec2(-1, 30))) {
                    showRegister = false;
                }

                if (ClientChat::GetInstance().HasRegisterResponse()) {
                    ImGui::Dummy(ImVec2(0, 6));
                    ImVec4 textCol = ClientChat::GetInstance().IsRegisterSuccess() ? ImVec4(0.3f, 1.f, 0.3f, 1.f) : ImVec4(1.f, 0.3f, 0.3f, 1.f);
                    ImGui::PushStyleColor(ImGuiCol_Text, textCol);
                    ImGui::TextWrapped("%s", ClientChat::GetInstance().GetRegisterMessage().c_str());
                    ImGui::PopStyleColor();
                }
            }

            // Error display
            if (ClientChat::GetInstance().HasConnectionFailed()) {
                ImGui::Dummy(ImVec2(0, 6));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.3f, 0.3f, 1.f));
                ImGui::TextWrapped("Hata: Sunucuya baglanamadi!");
                ImGui::PopStyleColor();
            } else if (!ClientChat::GetInstance().GetLoginError().empty()) {
                ImGui::Dummy(ImVec2(0, 6));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.3f, 0.3f, 1.f));
                ImGui::TextWrapped("Hata: %s", ClientChat::GetInstance().GetLoginError().c_str());
                ImGui::PopStyleColor();
            }
            
            ImGui::End();
            ImGui::PopStyleVar(4);
        }
        // ============================================================
        //  MAIN DASHBOARD
        // ============================================================
        else {
            RenderDashboard(io, messageBuffer, serverIp, dt);
        }

        // ============================================================
        //  TOASTS
        // ============================================================
        RenderToasts(io, dt);

        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);

        VkPipelineStageFlags wStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si = {}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount = 1; si.pWaitSemaphores = &g_SemImageAvail;
        si.pWaitDstStageMask  = &wStage;
        si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
        si.signalSemaphoreCount = 1; si.pSignalSemaphores = &g_SemRenderDone;
        vkQueueSubmit(g_Queue, 1, &si, g_FenceInFlight);

        VkPresentInfoKHR pi = {}; pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &g_SemRenderDone;
        pi.swapchainCount = 1; pi.pSwapchains = &g_Swapchain; pi.pImageIndices = &imageIndex;
        VkResult pr = vkQueuePresentKHR(g_Queue, &pi);
        if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) {
            vkDeviceWaitIdle(g_Device); DestroySwapchain();
            glfwGetFramebufferSize(window, &fw, &fh); CreateSwapchain(fw, fh);
        }
    }

    vkDeviceWaitIdle(g_Device);
    g_ScreenShareTexture.Destroy();
    g_LogoTexture.Destroy();
    for (int i = 0; i < AVATAR_COUNT; ++i) {
        g_AvatarTextures[i].Destroy();
    }
    ClientVoice::GetInstance().Close();
    ClientChat::GetInstance().Close();
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    DestroySwapchain();
    CleanupVulkan();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

static std::string GetUserRole(const std::string& username) {
    for (const auto& u : ClientChat::GetInstance().GetOnlineUsers()) {
        if (u.username == username) {
            return u.role;
        }
    }
    return "User";
}

// ================================================================
//  RenderDashboard — Role-based & Tabbed Redesign
// ================================================================
void RenderDashboard(const ImGuiIO& io, char* messageBuffer, char* serverIp, float dt)
{
    const std::string& role     = ClientAuth::GetInstance().GetRole();
    const std::string& username = ClientAuth::GetInstance().GetUsername();

    bool isRBG   = (role == "RBG");
    bool isOwner = (role == "Owner" || isRBG);
    bool isAdmin = (role == "Admin" || isOwner);
    bool isMod   = (role == "Mod"   || isAdmin);

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    ImGui::Begin("##Dashboard", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse);

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float itemSpacingX = ImGui::GetStyle().ItemSpacing.x;
    float itemSpacingY = ImGui::GetStyle().ItemSpacing.y;

    float bottomBarH = 40.f;
    float dividerW = 12.f;
    float sidebarW = 240.f;

    float contentH = avail.y - bottomBarH - itemSpacingY;
    if (contentH < 100.f) contentH = 100.f;

    float mainContentW = avail.x - sidebarW - dividerW - 2.f * itemSpacingX;
    if (mainContentW < 100.f) mainContentW = 100.f;

    // ---- MAIN CONTENT AREA (Left Panel, drawn first!) ----
    ImGui::BeginChild("##MainContent", ImVec2(mainContentW, contentH), true, ImGuiWindowFlags_NoScrollbar);

    // Profile Section inside Main Content Area (Top-Left)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.f, 4.f));
        ImGui::BeginGroup();
            ImVec2 cp = ImGui::GetCursorScreenPos();
            float r = 16.f;
            ImVec2 center(cp.x + r + 4, cp.y + r + 4);
            DrawAvatar(center, r, username, role);
            ImGui::Dummy(ImVec2(2 * r + 12, 2 * r + 8));
            ImGui::SameLine();
            ImGui::BeginGroup();
                ImGui::TextColored(ImVec4(1.f, 1.f, 1.f, 1.f), "%s", username.c_str());
                ImVec4 roleCol = ImVec4(0.7f, 0.7f, 0.7f, 1.f);
                if      (role == "RBG")   roleCol = ImVec4(1.0f, 0.6f, 0.0f, 1.f);
                else if (role == "Owner") roleCol = ImVec4(0.8f, 0.2f, 0.8f, 1.f);
                else if (role == "Admin") roleCol = ImVec4(1.0f, 0.3f, 0.3f, 1.f);
                else if (role == "Mod")   roleCol = ImVec4(0.2f, 0.8f, 0.2f, 1.f);
                ImGui::TextColored(roleCol, "[%s]", role.c_str());
            ImGui::EndGroup();
        ImGui::EndGroup();
        ImGui::PopStyleVar();
    }
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 4));

    // ============================================================
    //  TAB 1: CHAT ROOM
    // ============================================================
    if (g_ActiveTab == ActiveTab::CHAT) {
        float rightPanelW = g_ShowUserList ? 200.f : 0.f;
        float chatWidth = ImGui::GetContentRegionAvail().x - rightPanelW - (g_ShowUserList ? 8.f : 0.f);

        // Chat Container
        ImGui::BeginChild("##ChatContainer", ImVec2(chatWidth, 0), true, ImGuiWindowFlags_NoScrollbar);

        // Header
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.75f, 0.0f, 1.0f));
        ImGui::Text("# genel-sohbet");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::TextDisabled("RBG Yonetim Ekibi Resmi Sohbet Kanali");

        // User list toggle button on header far right
        ImGui::SameLine(ImGui::GetWindowWidth() - 110.f);
        if (ImGui::SmallButton(g_ShowUserList ? "Uyeleri Gizle" : "Uyeleri Goster")) {
            g_ShowUserList = !g_ShowUserList;
        }

        ImGui::Separator();

        // Messages scroll area
        bool isAnyOtherTyping = !ClientChat::GetInstance().GetTypingUsers().empty();
        float inputOffset = (g_ReplyToId > 0) ? -104.f : -74.f;
        if (isAnyOtherTyping) inputOffset -= 22.f;
        ImGui::BeginChild("##Msgs", ImVec2(0, inputOffset), true);
        for (const auto& msg : ClientChat::GetInstance().GetMessages()) {
            if (msg.sender == "[Sistem]") {
                ImGui::Spacing();
                float windowWidth = ImGui::GetContentRegionAvail().x;
                std::string text = "[Sistem] " + msg.content;
                float textWidth = ImGui::CalcTextSize(text.c_str()).x;
                ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
                
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.2f, 0.2f, 0.15f, 0.4f));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.f);
                std::string childId = "##sys_" + std::to_string(msg.timestamp) + "_" + msg.content;
                ImGui::BeginChild(childId.c_str(), ImVec2(textWidth + 24.f, 26.f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                ImGui::SetCursorPos(ImVec2(12.f, 4.f));
                ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.6f, 0.9f), "%s", text.c_str());
                ImGui::EndChild();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
                ImGui::Spacing();
            } else {
                ImGui::Spacing();
                
                // Draw Avatar
                ImVec2 cp = ImGui::GetCursorScreenPos();
                float radius = 16.f;
                ImVec2 avatarCenter(cp.x + radius + 4.f, cp.y + radius + 4.f);
                std::string senderRole = GetUserRole(msg.sender);
                DrawAvatar(avatarCenter, radius, msg.sender, senderRole);
                
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 2 * radius + 14.f);
                
                ImGui::BeginGroup();
                    ImVec4 uc = ImVec4(0.7f, 0.7f, 0.7f, 1.f);
                    if      (senderRole == "RBG")   uc = ImVec4(1.0f, 0.6f, 0.0f, 1.f);
                    else if (senderRole == "Owner") uc = ImVec4(0.8f, 0.2f, 0.8f, 1.f);
                    else if (senderRole == "Admin") uc = ImVec4(1.0f, 0.3f, 0.3f, 1.f);
                    else if (senderRole == "Mod")   uc = ImVec4(0.2f, 0.8f, 0.2f, 1.f);
                    
                    ImGui::TextColored(uc, "%s", msg.sender.c_str());
                    ImGui::SameLine();
                    
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(uc.x, uc.y, uc.z, 0.8f));
                    ImGui::TextDisabled("[%s]", senderRole.c_str());
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                    
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.45f, 0.48f, 1.f));
                    ImGui::Text("- %s", FormatTimestamp(msg.timestamp).c_str());
                    ImGui::PopStyleColor();
                    
                    // Reply bubble rendering
                    if (msg.reply_to_id > 0) {
                        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f, 0.16f, 0.20f, 0.8f));
                        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.f);
                        std::string replyChildId = "##reply_" + std::to_string(msg.id) + "_" + std::to_string(msg.timestamp);
                        std::string replyText = "> @" + msg.reply_sender + ": " + msg.reply_content;
                        float replyW = std::min(ImGui::GetContentRegionAvail().x - 120.f, ImGui::CalcTextSize(replyText.c_str()).x + 16.f);
                        ImGui::BeginChild(replyChildId.c_str(), ImVec2(replyW, 22.f), false, ImGuiWindowFlags_NoScrollbar);
                        ImGui::SetCursorPos(ImVec2(8, 2));
                        ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.9f, 0.9f), "%s", replyText.c_str());
                        ImGui::EndChild();
                        ImGui::PopStyleVar();
                        ImGui::PopStyleColor();
                    }

                    ImGui::Dummy(ImVec2(0.f, 2.f));
                    ImGui::TextWrapped("%s", msg.content.c_str());
                ImGui::EndGroup();

                // Reply Button on hover or aligned right
                ImGui::SameLine(ImGui::GetWindowWidth() - 75.f);
                std::string replyBtnId = "Yanitla##" + std::to_string(msg.id);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.42f, 0.78f, 0.2f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.42f, 0.78f, 0.6f));
                if (ImGui::SmallButton(replyBtnId.c_str())) {
                    g_ReplyToId = msg.id;
                    g_ReplyToSender = msg.sender;
                }
                ImGui::PopStyleColor(2);
                
                ImGui::Spacing();
                ImGui::Separator();
            }
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.f);
        ImGui::EndChild();

        // Reply Indicator Banner
        if (g_ReplyToId > 0) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.18f, 0.18f, 0.22f, 1.f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.f, 4.f));
            ImGui::BeginChildFrame(8899, ImVec2(ImGui::GetContentRegionAvail().x - 130.f, 26.f));
            ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.f), "> @%s kullanicisina yanit veriliyor...", g_ReplyToSender.c_str());
            ImGui::SameLine(ImGui::GetWindowWidth() - 35.f);
            if (ImGui::SmallButton("X##CancelReply")) {
                g_ReplyToId = 0;
                g_ReplyToSender = "";
            }
            ImGui::EndChildFrame();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();
        }

        // Typing Status Banner
        const auto& typingUsers = ClientChat::GetInstance().GetTypingUsers();
        if (!typingUsers.empty()) {
            std::string typingText = "";
            if (typingUsers.size() == 1) {
                typingText = typingUsers[0] + " yaziyor...";
            } else if (typingUsers.size() == 2) {
                typingText = typingUsers[0] + " ve " + typingUsers[1] + " yaziyor...";
            } else {
                typingText = "Birkac kisi yaziyor...";
            }
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.8f, 0.5f, 1.0f));
            ImGui::Text("%s", typingText.c_str());
            ImGui::PopStyleColor();
        }

        // Input Bar
        ImGui::Spacing();
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.f, 8.f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.18f, 1.f));
        
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 130.f);
        static bool reclaim_focus = false;
        if (reclaim_focus) {
            ImGui::SetKeyboardFocusHere(0);
            reclaim_focus = false;
        }
        
        bool enter = ImGui::InputTextWithHint("##msg", "Mesaj yazin veya mesaja yanit verin...", messageBuffer, 256, ImGuiInputTextFlags_EnterReturnsTrue);
        
        // Typing status update logic
        if (ImGui::IsItemActive() && strlen(messageBuffer) > 0) {
            g_MyTypingTimer += dt;
            if (!g_AmITyping) {
                g_AmITyping = true;
                ClientChat::GetInstance().SendTypingStatus(true);
            }
            if (g_MyTypingTimer >= 3.0f) {
                g_MyTypingTimer = 0.f;
                ClientChat::GetInstance().SendTypingStatus(true);
            }
        } else {
            if (g_AmITyping) {
                g_AmITyping = false;
                g_MyTypingTimer = 0.f;
                ClientChat::GetInstance().SendTypingStatus(false);
            }
        }

        ImGui::PopItemWidth();
        
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.42f, 0.78f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.52f, 0.90f, 1.f));
        bool send = ImGui::Button("Gonder", ImVec2(110, 32));
        ImGui::PopStyleColor(2);
        
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        
        if ((enter || send) && strlen(messageBuffer) > 0) {
            ClientChat::GetInstance().SendChatMessage(messageBuffer, g_ReplyToId);
            memset(messageBuffer, 0, 256);
            reclaim_focus = true;
            g_ReplyToId = 0;
            g_ReplyToSender = "";
            // Reset typing status immediately on send
            if (g_AmITyping) {
                g_AmITyping = false;
                g_MyTypingTimer = 0.f;
                ClientChat::GetInstance().SendTypingStatus(false);
            }
        }
        ImGui::EndChild();

        // Right side: online users sidebar (inside Chat tab)
        if (g_ShowUserList) {
            ImGui::SameLine();
            ImGui::BeginChild("##OnlineUsersChatList", ImVec2(rightPanelW, 0), true, ImGuiWindowFlags_NoScrollbar);
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.0f, 1.0f), "Cevrimici (%d)", (int)ClientChat::GetInstance().GetOnlineUsers().size());
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 4));
            
            for (const auto& u : ClientChat::GetInstance().GetOnlineUsers()) {
                ImVec4 uc = ImVec4(1.f, 1.f, 1.f, 1.f);
                if      (u.role == "RBG")   uc = ImVec4(1.0f, 0.6f, 0.0f, 1.f);
                else if (u.role == "Owner") uc = ImVec4(0.8f, 0.2f, 0.8f, 1.f);
                else if (u.role == "Admin") uc = ImVec4(1.0f, 0.3f, 0.3f, 1.f);
                else if (u.role == "Mod")   uc = ImVec4(0.2f, 0.8f, 0.2f, 1.f);

                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 p = ImGui::GetCursorScreenPos();
                dl->AddCircleFilled(ImVec2(p.x + 6, p.y + 8), 5.f, ImGui::ColorConvertFloat4ToU32(uc));
                ImGui::Dummy(ImVec2(14, 0)); ImGui::SameLine();
                ImGui::TextColored(uc, "%s", u.username.c_str());

                // Draw pencil if typing
                const auto& typingList = ClientChat::GetInstance().GetTypingUsers();
                if (std::find(typingList.begin(), typingList.end(), u.username) != typingList.end()) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "✏");
                }
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();
    }

    // ============================================================
    //  TAB 1.5: DMS (Direct Messages)
    // ============================================================
    else if (g_ActiveTab == ActiveTab::DMS) {
        float leftWidth = 240.f;
        float rightWidth = ImGui::GetContentRegionAvail().x - leftWidth - 8.f;

        // Sol panel: Direct message contacts list
        ImGui::BeginChild("##DMSContacts", ImVec2(leftWidth, 0), true);
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.0f, 1.0f), "Kisiler");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 4));

        std::vector<OnlineUser> otherUsers;
        for (const auto& u : ClientChat::GetInstance().GetOnlineUsers()) {
            if (u.username != username) {
                otherUsers.push_back(u);
            }
        }

        if (otherUsers.empty()) {
            ImGui::TextDisabled("Sohbet edecek kimse yok.");
        } else {
            for (const auto& u : otherUsers) {
                bool isSelected = (g_SelectedDMUser == u.username);
                if (isSelected) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.42f, 0.78f, 0.8f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.18f, 0.4f));
                }

                ImGui::PushID(u.username.c_str());
                ImVec2 cursor = ImGui::GetCursorScreenPos();
                
                // Align content
                if (ImGui::Button("##user_dm_btn", ImVec2(-1, 38))) {
                    g_SelectedDMUser = u.username;
                    ClientChat::GetInstance().SendGetDMHistory(g_SelectedDMUser);
                }
                
                ImGui::PopStyleColor();

                // Draw user avatar inside button
                ImGui::SetCursorScreenPos(ImVec2(cursor.x + 8.f, cursor.y + 5.f));
                DrawAvatar(ImVec2(cursor.x + 8.f + 14.f, cursor.y + 5.f + 14.f), 14.f, u.username, u.role, u.avatar_id);

                // Draw username text inside button
                ImGui::SetCursorScreenPos(ImVec2(cursor.x + 44.f, cursor.y + 9.f));
                ImVec4 uc = ImVec4(1.f, 1.f, 1.f, 1.f);
                if      (u.role == "RBG")   uc = ImVec4(1.0f, 0.6f, 0.0f, 1.f);
                else if (u.role == "Owner") uc = ImVec4(0.8f, 0.2f, 0.8f, 1.f);
                else if (u.role == "Admin") uc = ImVec4(1.0f, 0.3f, 0.3f, 1.f);
                else if (u.role == "Mod")   uc = ImVec4(0.2f, 0.8f, 0.2f, 1.f);
                ImGui::TextColored(uc, "%s", u.username.c_str());

                // Check typing
                const auto& typingList = ClientChat::GetInstance().GetTypingUsers();
                if (std::find(typingList.begin(), typingList.end(), u.username) != typingList.end()) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "✏");
                }

                ImGui::PopID();
                ImGui::Dummy(ImVec2(0, 2.f));
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Sağ panel: Active DM Chat window
        ImGui::BeginChild("##DMSWindow", ImVec2(rightWidth, 0), true, ImGuiWindowFlags_NoScrollbar);
        if (g_SelectedDMUser.empty()) {
            ImGui::Dummy(ImVec2(0, 100.f));
            float panelW = ImGui::GetContentRegionAvail().x;
            std::string hint = "Ozel mesajlasmak icin sol taraftan bir kullanici secin.";
            float textW = ImGui::CalcTextSize(hint.c_str()).x;
            ImGui::SetCursorPosX((panelW - textW) * 0.5f);
            ImGui::TextDisabled("%s", hint.c_str());
        } else {
            // Active User info header
            std::string userRole = "User";
            int userAvatar = 0;
            for (const auto& u : ClientChat::GetInstance().GetOnlineUsers()) {
                if (u.username == g_SelectedDMUser) {
                    userRole = u.role;
                    userAvatar = u.avatar_id;
                    break;
                }
            }

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.8f, 1.0f, 1.f));
            ImGui::Text("@ %s ile Ozel Mesajlasma", g_SelectedDMUser.c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled("[%s]", userRole.c_str());
            ImGui::Separator();

            // Message scroll container
            float typingOffset = 0.f;
            const auto& typingList = ClientChat::GetInstance().GetTypingUsers();
            bool isTargetTyping = (std::find(typingList.begin(), typingList.end(), g_SelectedDMUser) != typingList.end());
            
            float inputOffset = isTargetTyping ? -74.f : -52.f;
            ImGui::BeginChild("##DMMessages", ImVec2(0, inputOffset), true);

            for (const auto& msg : ClientChat::GetInstance().GetPrivateMessages()) {
                // Filter messages only between us and selected user
                if ((msg.sender == username && msg.to == g_SelectedDMUser) ||
                    (msg.sender == g_SelectedDMUser && msg.to == username)) {
                    
                    ImGui::Spacing();
                    
                    // Draw Avatar
                    ImVec2 cp = ImGui::GetCursorScreenPos();
                    float radius = 16.f;
                    ImVec2 avatarCenter(cp.x + radius + 4.f, cp.y + radius + 4.f);
                    std::string senderRole = (msg.sender == username) ? role : userRole;
                    int senderAvatar = (msg.sender == username) ? ClientChat::GetInstance().GetMyAvatarId() : userAvatar;
                    DrawAvatar(avatarCenter, radius, msg.sender, senderRole, senderAvatar);
                    
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 2 * radius + 14.f);
                    
                    ImGui::BeginGroup();
                        ImVec4 uc = ImVec4(0.7f, 0.7f, 0.7f, 1.f);
                        if      (senderRole == "RBG")   uc = ImVec4(1.0f, 0.6f, 0.0f, 1.f);
                        else if (senderRole == "Owner") uc = ImVec4(0.8f, 0.2f, 0.8f, 1.f);
                        else if (senderRole == "Admin") uc = ImVec4(1.0f, 0.3f, 0.3f, 1.f);
                        else if (senderRole == "Mod")   uc = ImVec4(0.2f, 0.8f, 0.2f, 1.f);
                        
                        ImGui::TextColored(uc, "%s", msg.sender.c_str());
                        ImGui::SameLine();
                        
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(uc.x, uc.y, uc.z, 0.8f));
                        ImGui::TextDisabled("[%s]", senderRole.c_str());
                        ImGui::PopStyleColor();
                        ImGui::SameLine();
                        
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.45f, 0.48f, 1.f));
                        ImGui::Text("- %s", FormatTimestamp(msg.timestamp).c_str());
                        ImGui::PopStyleColor();
                        
                        ImGui::Dummy(ImVec2(0.f, 2.f));
                        ImGui::TextWrapped("%s", msg.content.c_str());
                    ImGui::EndGroup();
                    
                    ImGui::Spacing();
                    ImGui::Separator();
                }
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.f);
            ImGui::EndChild();

            // Typing Status Banner for DM
            if (isTargetTyping) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.8f, 0.5f, 1.0f));
                ImGui::Text("%s yaziyor...", g_SelectedDMUser.c_str());
                ImGui::PopStyleColor();
                ImGui::Spacing();
            }

            // Input Bar
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.f, 8.f));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.18f, 1.f));
            
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 130.f);
            static bool dm_reclaim_focus = false;
            if (dm_reclaim_focus) {
                ImGui::SetKeyboardFocusHere(0);
                dm_reclaim_focus = false;
            }
            
            static char dmBuffer[256] = "";
            bool enter = ImGui::InputTextWithHint("##dm_msg", "Mesaj yazin...", dmBuffer, 256, ImGuiInputTextFlags_EnterReturnsTrue);
            
            // Typing status update logic for DM input
            if (ImGui::IsItemActive() && strlen(dmBuffer) > 0) {
                g_MyTypingTimer += dt;
                if (!g_AmITyping) {
                    g_AmITyping = true;
                    ClientChat::GetInstance().SendTypingStatus(true);
                }
                if (g_MyTypingTimer >= 3.0f) {
                    g_MyTypingTimer = 0.f;
                    ClientChat::GetInstance().SendTypingStatus(true);
                }
            } else {
                if (g_AmITyping) {
                    g_AmITyping = false;
                    g_MyTypingTimer = 0.f;
                    ClientChat::GetInstance().SendTypingStatus(false);
                }
            }

            ImGui::PopItemWidth();
            
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.42f, 0.78f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.52f, 0.90f, 1.f));
            bool send = ImGui::Button("Gonder##DMSend", ImVec2(110, 32));
            ImGui::PopStyleColor(2);
            
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            
            if ((enter || send) && strlen(dmBuffer) > 0) {
                ClientChat::GetInstance().SendPrivateMessage(g_SelectedDMUser, dmBuffer);
                memset(dmBuffer, 0, 256);
                dm_reclaim_focus = true;
                if (g_AmITyping) {
                    g_AmITyping = false;
                    g_MyTypingTimer = 0.f;
                    ClientChat::GetInstance().SendTypingStatus(false);
                }
            }
        }
        ImGui::EndChild();
    }

    // ============================================================
    //  TAB 2: VOICE CALL & SCREEN SHARING
    // ============================================================
    else if (g_ActiveTab == ActiveTab::VOICE) {
        float leftWidth = ImGui::GetContentRegionAvail().x - 260.f;
        
        // Call Panel
        ImGui::BeginChild("##VoiceMainPanel", ImVec2(leftWidth, 0), true, ImGuiWindowFlags_NoScrollbar);
        ImGui::Text("Sesli ve Ekran Paylasim Odasi");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 10));

        if (ClientVoice::GetInstance().IsInCall()) {
            std::string callUser = ClientVoice::GetInstance().GetCurrentCallUser();
            
            if (ClientVoice::GetInstance().IsReceivingScreen()) {
                // Live Screen Render
                if (g_ScreenShareTexture.DescriptorSet != VK_NULL_HANDLE) {
                    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.f), "%s kullanicisinin yayini:", callUser.c_str());
                    
                    float frameW = 640.f, frameH = 360.f;
                    float availW = ImGui::GetContentRegionAvail().x;
                    if (availW < frameW) {
                        frameW = availW - 20.f;
                        frameH = frameW * (270.f / 480.f);
                    }
                    ImGui::Image((ImTextureID)g_ScreenShareTexture.DescriptorSet, ImVec2(frameW, frameH));
                } else {
                    ImGui::TextDisabled("Yayina baglaniliyor, ekran bekleniyor...");
                }
            } else {
                // Standard audio call view
                ImGui::Spacing();
                float centerPos = (ImGui::GetContentRegionAvail().x - 120.f) * 0.5f;
                ImGui::SetCursorPosX(centerPos);
                
                // Pulsing visual indicator around large avatar
                ImVec2 cp = ImGui::GetCursorScreenPos();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                float r = 60.f;
                ImVec2 center(cp.x + r, cp.y + r);
                DrawAvatar(center, r, callUser, GetUserRole(callUser));
                
                static float wave = 0.f;
                wave += 0.05f;
                float pulseR = r + 10.f + 5.f * sin(wave);
                dl->AddCircle(center, pulseR, IM_COL32(0, 220, 0, 100), 0, 2.f);
                
                ImGui::Dummy(ImVec2(0, 2 * r + 20));
                
                std::string callInfo = callUser + " ile sesli sohbet baglantisi kuruldu.";
                float textW = ImGui::CalcTextSize(callInfo.c_str()).x;
                ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - textW) * 0.5f);
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.f), "%s", callInfo.c_str());
            }

            ImGui::Dummy(ImVec2(0, 30));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 10));

            // Controls
            float btnW = (ImGui::GetContentRegionAvail().x - 24.f) / 3.f;

            // 1. Mute
            bool isMuted = ClientVoice::GetInstance().IsMuted();
            if (isMuted) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.4f, 0.1f, 0.7f));
            else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.38f, 0.68f, 0.6f));
            if (ImGui::Button(isMuted ? "Sesi Ac" : "Sustur", ImVec2(btnW, 36))) {
                ClientVoice::GetInstance().SetMuted(!isMuted);
                AddToast(!isMuted ? "Mikrofon kapatildi" : "Mikrofon acildi", ToastType::INFO);
            }
            ImGui::PopStyleColor();

            ImGui::SameLine();

            // 2. Screen share
            bool isSharing = ClientVoice::GetInstance().IsScreenSharing();
            if (isSharing) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.3f, 0.8f));
            else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.14f, 0.18f, 0.8f));
            if (ImGui::Button(isSharing ? "Yayini Kapat" : "Ekran Paylas", ImVec2(btnW, 36))) {
                ClientVoice::GetInstance().SetScreenSharing(!isSharing);
                AddToast(!isSharing ? "Ekran paylasimi baslatildi" : "Ekran paylasimi sonlandirildi", ToastType::INFO);
            }
            ImGui::PopStyleColor();

            ImGui::SameLine();

            // 3. Hangup
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 0.8f));
            if (ImGui::Button("Sonlandir", ImVec2(btnW, 36))) {
                ClientVoice::GetInstance().StopCall();
                AddToast("Aramadan cikildi", ToastType::INFO);
            }
            ImGui::PopStyleColor();

        } else if (ClientVoice::GetInstance().IsCalling()) {
            // Calling state UI
            ImGui::Spacing();
            float centerPos = (ImGui::GetContentRegionAvail().x - 120.f) * 0.5f;
            ImGui::SetCursorPosX(centerPos);
            
            std::string callUser = ClientVoice::GetInstance().GetCurrentCallUser();
            
            ImVec2 cp = ImGui::GetCursorScreenPos();
            float r = 60.f;
            ImVec2 center(cp.x + r, cp.y + r);
            DrawAvatar(center, r, callUser, GetUserRole(callUser));
            
            ImGui::Dummy(ImVec2(0, 2 * r + 20));
            
            std::string callInfo = callUser + " araniyor...";
            float textW = ImGui::CalcTextSize(callInfo.c_str()).x;
            ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - textW) * 0.5f);
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.f), "%s", callInfo.c_str());
            
            ImGui::Dummy(ImVec2(0, 30));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 10));
            
            float btnW = ImGui::GetContentRegionAvail().x - 24.f;
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 0.8f));
            if (ImGui::Button("Aramayi Iptal Et", ImVec2(btnW, 36))) {
                ClientVoice::GetInstance().StopCall();
                AddToast("Arama iptal edildi", ToastType::INFO);
            }
            ImGui::PopStyleColor();

        } else {
            ImGui::Dummy(ImVec2(0, 40));
            float panelW = ImGui::GetContentRegionAvail().x;
            std::string hint = "Sesli veya ekran paylasimli arama baslatmak icin\nsagdaki listeden bir arkadasinizi secin.";
            float textW = ImGui::CalcTextSize("Sesli veya ekran paylasimli arama baslatmak icin").x;
            ImGui::SetCursorPosX((panelW - textW) * 0.5f);
            ImGui::TextWrapped("Sesli veya ekran paylasimli arama baslatmak icin\nsagdaki listeden bir arkadasinizi secip 'Ara' butonuna basin.");
        }
        ImGui::EndChild();

        // Right side: dial list
        ImGui::SameLine();
        ImGui::BeginChild("##VoiceContactsList", ImVec2(252, 0), true, ImGuiWindowFlags_NoScrollbar);
        ImGui::Text("Baglanti Listesi");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 4));

        std::vector<std::string> targetUsers;
        for (const auto& u : ClientChat::GetInstance().GetOnlineUsers()) {
            if (u.username != username) {
                targetUsers.push_back(u.username);
            }
        }

        if (targetUsers.empty()) {
            ImGui::TextDisabled("Arayacak cevrimici üye yok.");
        } else {
            for (const auto& target : targetUsers) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.f, 1.f, 1.f, 1.f), "%s", target.c_str());
                ImGui::SameLine(ImGui::GetWindowWidth() - 75.f);
                
                std::string callBtnId = "Ara##" + target;
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.6f, 0.3f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.7f, 0.35f, 1.0f));
                if (ImGui::SmallButton(callBtnId.c_str())) {
                    ClientVoice::GetInstance().StartCall(target);
                    AddToast("Arama baslatiliyor...", ToastType::INFO);
                }
                ImGui::PopStyleColor(2);
            }
        }
        ImGui::EndChild();
    }

    // ============================================================
    //  TAB 3: MODERATION PANEL (Mod+)
    // ============================================================
    else if (g_ActiveTab == ActiveTab::MODERATION && isMod) {
        ImGui::BeginChild("##ModerationPanel", ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar);
        ImGui::TextColored(ImVec4(1.f, 0.3f, 0.3f, 1.f), "Yetkili Yonetim Paneli");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 8));

        ImGui::Text("Sunucudaki aktif kullanıcıları kontrol edebilir, yetkilerine göre eylem uygulayabilirsiniz.");
        ImGui::Dummy(ImVec2(0, 6));

        if (ImGui::BeginTable("##UserModerationTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Kullanici Adi", ImGuiTableColumnFlags_WidthFixed, 200.f);
            ImGui::TableSetupColumn("Rol", ImGuiTableColumnFlags_WidthFixed, 150.f);
            ImGui::TableSetupColumn("Yonetim Eylemleri", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (const auto& u : ClientChat::GetInstance().GetOnlineUsers()) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                
                ImVec4 uc = ImVec4(1.f, 1.f, 1.f, 1.f);
                if      (u.role == "RBG")   uc = ImVec4(1.0f, 0.6f, 0.0f, 1.f);
                else if (u.role == "Owner") uc = ImVec4(0.8f, 0.2f, 0.8f, 1.f);
                else if (u.role == "Admin") uc = ImVec4(1.0f, 0.3f, 0.3f, 1.f);
                else if (u.role == "Mod")   uc = ImVec4(0.2f, 0.8f, 0.2f, 1.f);
                
                ImGui::TextColored(uc, "%s", u.username.c_str());
                if (u.username == username) {
                    ImGui::SameLine();
                    ImGui::TextDisabled(" (Siz)");
                }

                ImGui::TableNextColumn();
                ImGui::TextColored(uc, "%s", u.role.c_str());

                ImGui::TableNextColumn();
                if (u.username != username) {
                    // Kick button
                    std::string modKickId = "Sunucudan At (Kick)##" + u.username;
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 0.6f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.15f, 0.15f, 1.f));
                    if (ImGui::SmallButton(modKickId.c_str())) {
                        ClientChat::GetInstance().SendKick(u.username);
                        AddToast("Kick gönderildi: " + u.username, ToastType::WARNING);
                    }
                    ImGui::PopStyleColor(2);

                    ImGui::SameLine();

                    // Sustur (Mute) placeholder button
                    std::string modMuteId = "Sustur (Mute)##" + u.username;
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.4f, 0.1f, 0.5f));
                    if (ImGui::SmallButton(modMuteId.c_str())) {
                        AddToast("Kullanici susturuldu (Mock)", ToastType::INFO);
                    }
                    ImGui::PopStyleColor();
                } else {
                    ImGui::TextDisabled("Kendi hesabiniza eylem uygulayamazsiniz.");
                }
            }
            ImGui::EndTable();
        }

        // Onay bekleyen hesaplar tablosu
        ImGui::Dummy(ImVec2(0, 14));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 14));

        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.8f, 1.f), "Onay Bekleyen Hesaplar");
        ImGui::Dummy(ImVec2(0, 6));

        const auto& pending = ClientChat::GetInstance().GetPendingUsers();
        if (pending.empty()) {
            ImGui::TextDisabled("Onay bekleyen yeni hesap bulunmamaktadır.");
        } else {
            if (ImGui::BeginTable("##PendingApprovalsTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("Kullanici Adi", ImGuiTableColumnFlags_WidthFixed, 300.f);
                ImGui::TableSetupColumn("Islemler", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                for (const auto& pendingUser : pending) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", pendingUser.c_str());

                    ImGui::TableNextColumn();
                    
                    // Approve button
                    std::string approveBtnId = "Onayla##" + pendingUser;
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.6f, 0.1f, 0.7f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.8f, 0.15f, 1.f));
                    if (ImGui::SmallButton(approveBtnId.c_str())) {
                        ClientChat::GetInstance().SendApproveUser(pendingUser);
                        AddToast("Kullanici onaylandi: " + pendingUser, ToastType::SUCCESS);
                    }
                    ImGui::PopStyleColor(2);

                    ImGui::SameLine();

                    // Reject button
                    std::string rejectBtnId = "Reddet##" + pendingUser;
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 0.7f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.15f, 0.15f, 1.f));
                    if (ImGui::SmallButton(rejectBtnId.c_str())) {
                        ClientChat::GetInstance().SendRejectUser(pendingUser);
                        AddToast("Kullanici reddedildi: " + pendingUser, ToastType::WARNING);
                    }
                    ImGui::PopStyleColor(2);
                }
                ImGui::EndTable();
            }
        }

        ImGui::EndChild();
    }

    // ============================================================
    //  TAB 4: SETTINGS & VISUAL PREFERENCES
    // ============================================================
    else if (g_ActiveTab == ActiveTab::SETTINGS) {
        ImGui::BeginChild("##SettingsPanel", ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar);
        ImGui::Text("Ayarlar ve Bilgiler");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 10));

        // 1. Theme Configuration
        ImGui::Text("Görsel Tema Seçimi:");
        ImGui::Dummy(ImVec2(0, 4));
        if (ImGui::RadioButton("Karanlık Tema (Premium Dark)", g_DarkMode)) {
            g_DarkMode = true;
            ApplyDarkTheme();
            AddToast("Karanlik tema aktif edildi", ToastType::SUCCESS);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Aydınlık Tema (Clean Light)", !g_DarkMode)) {
            g_DarkMode = false;
            ApplyLightTheme();
            AddToast("Aydinlik tema aktif edildi", ToastType::SUCCESS);
        }

        ImGui::Dummy(ImVec2(0, 14));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 14));

        // 1.5. Avatar Picker
        ImGui::Text("Profil Resmi (Avatar) Seçimi:");
        ImGui::Dummy(ImVec2(0, 6));

        int currentAvatar = ClientChat::GetInstance().GetMyAvatarId();
        
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.f, 10.f));
        
        // Default Avatar Button
        bool isDefaultSelected = (currentAvatar == 0);
        if (isDefaultSelected) ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 0.75f, 0.0f, 1.0f));
        else ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.f);

        ImGui::BeginGroup();
        if (ImGui::Button("Varsayilan##DefaultAvatar", ImVec2(72.f, 72.f))) {
            ClientChat::GetInstance().SendChangeAvatar(0);
            AddToast("Varsayilan avatar secildi", ToastType::SUCCESS);
        }
        ImGui::EndGroup();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        // Custom Avatars
        for (int i = 0; i < AVATAR_COUNT; ++i) {
            ImGui::SameLine();
            bool isSelected = (currentAvatar == i + 1);
            if (isSelected) ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 0.75f, 0.0f, 1.0f));
            else ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.f);
            
            ImGui::BeginGroup();
            if (g_AvatarTextures[i].DescriptorSet != VK_NULL_HANDLE) {
                // Draw image with active status checking
                ImGui::PushID(i);
                if (ImGui::ImageButton((ImTextureID)g_AvatarTextures[i].DescriptorSet, ImVec2(56.f, 56.f))) {
                    ClientChat::GetInstance().SendChangeAvatar(i + 1);
                    AddToast("Profil resmi güncellendi", ToastType::SUCCESS);
                }
                ImGui::PopID();
            } else {
                std::string btnLabel = std::to_string(i + 1) + "##custom_av";
                if (ImGui::Button(btnLabel.c_str(), ImVec2(72.f, 72.f))) {
                    ClientChat::GetInstance().SendChangeAvatar(i + 1);
                    AddToast("Profil resmi güncellendi", ToastType::SUCCESS);
                }
            }
            ImGui::EndGroup();
            
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }
        ImGui::PopStyleVar(2);

        ImGui::Dummy(ImVec2(0, 14));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 14));

        // 2. Change Password UI
        ImGui::Text("Şifre Değiştirme:");
        ImGui::Dummy(ImVec2(0, 6));
        
        static char oldPwd[64] = "";
        static char newPwd[64] = "";
        static char confirmPwd[64] = "";

        ImGui::Text("Mevcut Şifre:");
        ImGui::SetNextItemWidth(300.f);
        ImGui::InputText("##oldpwd", oldPwd, sizeof(oldPwd), ImGuiInputTextFlags_Password);
        ImGui::Dummy(ImVec2(0, 4));

        ImGui::Text("Yeni Şifre:");
        ImGui::SetNextItemWidth(300.f);
        ImGui::InputText("##newpwd1", newPwd, sizeof(newPwd), ImGuiInputTextFlags_Password);
        ImGui::Dummy(ImVec2(0, 4));

        ImGui::Text("Yeni Şifre Tekrar:");
        ImGui::SetNextItemWidth(300.f);
        ImGui::InputText("##newpwd2", confirmPwd, sizeof(confirmPwd), ImGuiInputTextFlags_Password);
        ImGui::Dummy(ImVec2(0, 10));

        if (ImGui::Button("Şifreyi Güncelle", ImVec2(150.f, 32.f))) {
            if (strlen(newPwd) == 0 || strlen(oldPwd) == 0) {
                AddToast("Şifre alanları boş bırakılamaz!", ToastType::ERR);
            } else if (strcmp(newPwd, confirmPwd) != 0) {
                AddToast("Yeni şifreler uyuşmuyor!", ToastType::ERR);
            } else {
                AddToast("Şifre başarıyla güncellendi! (Mock)", ToastType::SUCCESS);
                memset(oldPwd, 0, 64);
                memset(newPwd, 0, 64);
                memset(confirmPwd, 0, 64);
            }
        }

        ImGui::Dummy(ImVec2(0, 14));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 14));

        // 3. Server & Client Connection Details
        ImGui::Text("Bağlantı Bilgileri:");
        ImGui::BulletText("İstemci Versiyonu: v2.0-Redesign");
        ImGui::BulletText("Geliştirici: RBG Team Yazılım Ekibi");

        ImGui::EndChild();
    }

    ImGui::EndChild(); // End MainContent

    ImGui::SameLine();

    // ---- VERTICAL ROUNDED DIVIDER ----
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.25f, 0.25f, 0.28f, 0.6f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.f);
    ImGui::BeginChild("##VerticalDivider", ImVec2(dividerW, contentH), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::SameLine();

    // ---- RIGHT SIDEBAR (240px) ----
    ImGui::BeginChild("##Sidebar", ImVec2(sidebarW, contentH), true, ImGuiWindowFlags_NoScrollbar);

    // Sidebar Logo Header
    if (g_LogoTexture.DescriptorSet != VK_NULL_HANDLE) {
        ImGui::SetCursorPosX((sidebarW - 56.f) * 0.5f - 8.f);
        ImGui::Image((ImTextureID)g_LogoTexture.DescriptorSet, ImVec2(56.f, 56.f));
        ImGui::Dummy(ImVec2(0, 4));
    }

    ImGui::TextDisabled("Odalar & Yonetim");
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 4));

    // Vertical Tab Button Helper
    auto RenderTabButton = [](const char* label, ActiveTab tab, ActiveTab& activeTab) {
        bool isSelected = (activeTab == tab);
        if (isSelected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.42f, 0.78f, 1.0f)); // Selected color
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.42f, 0.78f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14f, 0.35f, 0.68f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.14f, 0.18f, 0.0f)); // Transparent
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.18f, 0.24f, 0.4f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.18f, 0.24f, 0.6f));
        }
        
        if (ImGui::Button(label, ImVec2(-1, 38))) {
            activeTab = tab;
        }
        ImGui::PopStyleColor(3);
        ImGui::Dummy(ImVec2(0, 4));
    };

    // Navigation Tabs
    RenderTabButton("Genel Sohbet", ActiveTab::CHAT, g_ActiveTab);
    RenderTabButton("Ozel Mesajlar", ActiveTab::DMS, g_ActiveTab);
    RenderTabButton("Sesli Sohbet", ActiveTab::VOICE, g_ActiveTab);
    if (isMod) {
        RenderTabButton("Yonetim Paneli", ActiveTab::MODERATION, g_ActiveTab);
    }
    RenderTabButton("Ayarlar & Tema", ActiveTab::SETTINGS, g_ActiveTab);

    ImGui::Dummy(ImVec2(0, 8));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 8));

    // Sidebar Bottom Info & Utilities
    float winHeight = ImGui::GetWindowHeight();
    float curY = ImGui::GetCursorPosY();
    float targetY = winHeight - 120.f; // Reserve space for clock, ping and logout
    if (targetY > curY) {
        ImGui::Dummy(ImVec2(0, targetY - curY));
    }

    // Digital Clock
    {
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&tt), "%H:%M:%S");
        ImGui::TextColored(ImVec4(0.f, 0.9f, 0.9f, 1.f), "Saat: %s", ss.str().c_str());
    }

    // Latency (Ping)
    {
        uint32_t ping = ClientChat::GetInstance().GetPing();
        ImVec4 pingColor = ImVec4(0.3f, 0.9f, 0.3f, 1.0f); // Green
        if (ping > 150) pingColor = ImVec4(0.9f, 0.6f, 0.1f, 1.0f); // Orange
        if (ping > 300) pingColor = ImVec4(0.9f, 0.3f, 0.3f, 1.0f); // Red
        ImGui::TextColored(pingColor, "Ping: %d ms", ping);
    }

    ImGui::Dummy(ImVec2(0, 4));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 4));

    // Logout
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 0.3f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.15f, 0.15f, 0.8f));
    if (ImGui::Button("Cikis Yap", ImVec2(-1, 32))) {
        ClientChat::GetInstance().Disconnect();
        AddToast("Cikis yapildi", ToastType::INFO);
    }
    ImGui::PopStyleColor(2);

    ImGui::EndChild(); // End Sidebar

    // Spacer between content panels and bottom bar
    // ImGui automatic layout spacing is enough. No dummy needed to prevent overflow.

    // ---- BOTTOM ACTIVITY BAR ----
    ImGui::BeginChild("##BottomActivityBar", ImVec2(avail.x, bottomBarH), true, ImGuiWindowFlags_NoScrollbar);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 0.9f), "Aktiflik:");
    ImGui::SameLine();
    for (const auto& u : ClientChat::GetInstance().GetOnlineUsers()) {
        ImVec4 uc = ImVec4(1.f, 1.f, 1.f, 1.f);
        if      (u.role == "RBG")   uc = ImVec4(1.0f, 0.6f, 0.0f, 1.f);
        else if (u.role == "Owner") uc = ImVec4(0.8f, 0.2f, 0.8f, 1.f);
        else if (u.role == "Admin") uc = ImVec4(1.0f, 0.3f, 0.3f, 1.f);
        else if (u.role == "Mod")   uc = ImVec4(0.2f, 0.8f, 0.2f, 1.f);

        // Dot + Username
        ImGui::TextColored(uc, "● %s", u.username.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("[%s]", u.role.c_str());
        ImGui::SameLine(0, 16.f); // Gap of 16px between users
    }
    ImGui::EndChild(); // End BottomActivityBar

    // ---- INCOMING CALL POPUP OVERLAY ----
    if (ClientVoice::GetInstance().IsIncomingCallPending()) {
        std::string caller = ClientVoice::GetInstance().GetIncomingCallUser();
        
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(320, 160), ImGuiCond_Always);
        
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.f, 16.f));
        
        ImGui::Begin("Gelen Arama", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize);
        
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.f), "Gelen Sesli Arama");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 10));
        
        ImGui::Text("%s sizi ariyor...", caller.c_str());
        ImGui::Dummy(ImVec2(0, 15));
        
        float btnW = (ImGui::GetContentRegionAvail().x - 12.f) / 2.f;
        
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.6f, 0.3f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.7f, 0.35f, 1.0f));
        if (ImGui::Button("Ac", ImVec2(btnW, 36))) {
            ClientVoice::GetInstance().AcceptIncomingCall();
        }
        ImGui::PopStyleColor(2);
        
        ImGui::SameLine();
        
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Kapat", ImVec2(btnW, 36))) {
            ClientVoice::GetInstance().RejectIncomingCall();
        }
        ImGui::PopStyleColor(2);
        
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    ImGui::End(); // End Dashboard
}

// ================================================================
//  DrawAvatar — colored circle with initial letter
// ================================================================
void DrawAvatar(ImVec2 center, float radius, const std::string& username, const std::string& role, int avatarId)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Resolve avatarId if not provided
    int avatar = avatarId;
    if (avatar == -1) {
        if (username == ClientAuth::GetInstance().GetUsername()) {
            avatar = ClientChat::GetInstance().GetMyAvatarId();
        } else {
            avatar = 0;
            for (const auto& u : ClientChat::GetInstance().GetOnlineUsers()) {
                if (u.username == username) {
                    avatar = u.avatar_id;
                    break;
                }
            }
        }
    }

    // Outer glow
    dl->AddCircleFilled(center, radius + 3, IM_COL32(255,255,255,30));

    if (avatar > 0 && avatar <= AVATAR_COUNT && g_AvatarTextures[avatar - 1].DescriptorSet != VK_NULL_HANDLE) {
        ImVec2 p_min(center.x - radius, center.y - radius);
        ImVec2 p_max(center.x + radius, center.y + radius);
        dl->AddImage((ImTextureID)g_AvatarTextures[avatar - 1].DescriptorSet, p_min, p_max);
        
        // Circular border outline matching role
        ImU32 borderCol = IM_COL32(80,80,80,255);
        if      (role=="RBG")   borderCol = IM_COL32(200,120,0,255);
        else if (role=="Owner") borderCol = IM_COL32(180,40,180,255);
        else if (role=="Admin") borderCol = IM_COL32(220,50,50,255);
        else if (role=="Mod")   borderCol = IM_COL32(40,180,40,255);
        dl->AddCircle(center, radius, borderCol, 0, 1.5f);
    } else {
        // Role-based color
        ImU32 bg = IM_COL32(80,80,80,255);
        if      (role=="RBG")   bg = IM_COL32(200,120,0,255);
        else if (role=="Owner") bg = IM_COL32(180,40,180,255);
        else if (role=="Admin") bg = IM_COL32(220,50,50,255);
        else if (role=="Mod")   bg = IM_COL32(40,180,40,255);

        dl->AddCircleFilled(center, radius, bg);

        // Initial letter
        if (!username.empty()) {
            std::string init = "";
            unsigned char c = (unsigned char)username[0];
            if (c < 0x80) {
                init += (char)toupper(c);
            } else {
                size_t len = 0;
                if ((c & 0xE0) == 0xC0) len = 2;
                else if ((c & 0xF0) == 0xE0) len = 3;
                else if ((c & 0xF8) == 0xF0) len = 4;
                
                if (len > 0 && username.length() >= len) {
                    init = username.substr(0, len);
                } else {
                    init += (char)toupper(c);
                }
            }
            ImVec2 ts = ImGui::CalcTextSize(init.c_str());
            dl->AddText(ImVec2(center.x - ts.x*0.5f, center.y - ts.y*0.5f),
                        IM_COL32(255,255,255,255), init.c_str());
        }
    }
}

// ================================================================
//  RenderToasts — bottom-right corner notifications
// ================================================================
void RenderToasts(const ImGuiIO& io, float dt)
{
    // Update timers
    for (auto& t : g_Toasts) t.timeLeft -= dt;
    g_Toasts.erase(std::remove_if(g_Toasts.begin(), g_Toasts.end(),
        [](const Toast& t){ return t.timeLeft <= 0; }), g_Toasts.end());

    const float toastW = 300.f, toastH = 48.f, pad = 12.f, gap = 8.f;
    float y = io.DisplaySize.y - pad;

    for (int i = (int)g_Toasts.size()-1; i >= 0; --i) {
        auto& t = g_Toasts[i];
        float alpha = std::min(1.0f, t.timeLeft);           // fade out last second
        float slideIn = std::min(1.0f, (t.duration - t.timeLeft) * 8.f); // slide in

        y -= toastH;
        float x = io.DisplaySize.x - toastW - pad - (1.f - slideIn) * (toastW + pad);

        ImGui::SetNextWindowPos(ImVec2(x, y));
        ImGui::SetNextWindowSize(ImVec2(toastW, toastH));
        ImGui::SetNextWindowBgAlpha(0.92f * alpha);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);

        // Border color by type
        ImVec4 borderCol;
        const char* icon;
        switch(t.type) {
            case ToastType::SUCCESS: borderCol = ImVec4(0.2f,0.8f,0.2f,alpha); icon = "[OK]"; break;
            case ToastType::WARNING: borderCol = ImVec4(1.0f,0.7f,0.0f,alpha); icon = "[!]";  break;
            case ToastType::ERR:     borderCol = ImVec4(1.0f,0.3f,0.3f,alpha); icon = "[X]";  break;
            default:                 borderCol = ImVec4(0.3f,0.6f,1.0f,alpha); icon = "[i]";  break;
        }
        ImGui::PushStyleColor(ImGuiCol_Border, borderCol);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.f);

        std::string wname = "##toast" + std::to_string(i);
        ImGui::Begin(wname.c_str(), nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoNav        | ImGuiWindowFlags_NoMove   |
            ImGuiWindowFlags_NoSavedSettings);

        ImGui::SetCursorPos(ImVec2(10, (toastH - ImGui::GetTextLineHeight())*0.5f));
        ImGui::TextColored(borderCol, "%s", icon);
        ImGui::SameLine(42);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f,1.f,1.f,alpha));
        ImGui::TextUnformatted(t.message.c_str());
        ImGui::PopStyleColor();

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);

        y -= gap;
    }
}

// ================================================================
//  Vulkan setup helpers
// ================================================================
bool SetupVulkan(GLFWwindow* window)
{
    // Instance
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "RBGPanel";
    appInfo.apiVersion = VK_API_VERSION_1_2;

    uint32_t extCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&extCount);
    if (!glfwExts) { std::cerr << "No Vulkan extensions from GLFW" << std::endl; return false; }

    VkInstanceCreateInfo instCI = {};
    instCI.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instCI.pApplicationInfo = &appInfo;
    instCI.enabledExtensionCount = extCount;
    instCI.ppEnabledExtensionNames = glfwExts;
    if (vkCreateInstance(&instCI, nullptr, &g_Instance) != VK_SUCCESS) {
        std::cerr << "vkCreateInstance failed" << std::endl; return false;
    }

    if (glfwCreateWindowSurface(g_Instance, window, nullptr, &g_Surface) != VK_SUCCESS) {
        std::cerr << "Surface failed" << std::endl; return false;
    }

    // Physical device
    uint32_t dc = 0; vkEnumeratePhysicalDevices(g_Instance, &dc, nullptr);
    if (!dc) { std::cerr << "No GPU" << std::endl; return false; }
    std::vector<VkPhysicalDevice> devs(dc);
    vkEnumeratePhysicalDevices(g_Instance, &dc, devs.data());
    g_PhysDev = devs[0];

    // Queue family (graphics + present)
    uint32_t qfc = 0; vkGetPhysicalDeviceQueueFamilyProperties(g_PhysDev, &qfc, nullptr);
    std::vector<VkQueueFamilyProperties> qfs(qfc);
    vkGetPhysicalDeviceQueueFamilyProperties(g_PhysDev, &qfc, qfs.data());
    g_QueueFamily = UINT32_MAX;
    for (uint32_t i = 0; i < qfc; ++i) {
        VkBool32 ps = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(g_PhysDev, i, g_Surface, &ps);
        if ((qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && ps) { g_QueueFamily = i; break; }
    }
    if (g_QueueFamily == UINT32_MAX) { std::cerr << "No queue family" << std::endl; return false; }

    float qp = 1.f;
    VkDeviceQueueCreateInfo qCI = {};
    qCI.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qCI.queueFamilyIndex = g_QueueFamily; qCI.queueCount = 1; qCI.pQueuePriorities = &qp;

    const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo devCI = {};
    devCI.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    devCI.queueCreateInfoCount = 1; devCI.pQueueCreateInfos = &qCI;
    devCI.enabledExtensionCount = 1; devCI.ppEnabledExtensionNames = devExts;
    if (vkCreateDevice(g_PhysDev, &devCI, nullptr, &g_Device) != VK_SUCCESS) {
        std::cerr << "vkCreateDevice failed" << std::endl; return false;
    }
    vkGetDeviceQueue(g_Device, g_QueueFamily, 0, &g_Queue);

    // Command pool
    VkCommandPoolCreateInfo cpCI = {};
    cpCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpCI.queueFamilyIndex = g_QueueFamily;
    cpCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(g_Device, &cpCI, nullptr, &g_CmdPool);

    // Descriptor pool
    VkDescriptorPoolSize ps[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER,1000},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1000},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,1000},{VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1000},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,1000},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,1000}
    };
    VkDescriptorPoolCreateInfo dpCI = {};
    dpCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpCI.maxSets = 11000; dpCI.poolSizeCount = 11; dpCI.pPoolSizes = ps;
    vkCreateDescriptorPool(g_Device, &dpCI, nullptr, &g_DescPool);

    // Sync
    VkSemaphoreCreateInfo semCI = {}; semCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    vkCreateSemaphore(g_Device, &semCI, nullptr, &g_SemImageAvail);
    vkCreateSemaphore(g_Device, &semCI, nullptr, &g_SemRenderDone);
    VkFenceCreateInfo fCI = {}; fCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(g_Device, &fCI, nullptr, &g_FenceInFlight);
    return true;
}

bool CreateSwapchain(int width, int height)
{
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_PhysDev, g_Surface, &caps);
    g_SwapExtent.width  = std::max(caps.minImageExtent.width,  std::min(caps.maxImageExtent.width,  (uint32_t)width));
    g_SwapExtent.height = std::max(caps.minImageExtent.height, std::min(caps.maxImageExtent.height, (uint32_t)height));

    uint32_t imgCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount) imgCount = caps.maxImageCount;

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_PhysDev, g_Surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_PhysDev, g_Surface, &fmtCount, fmts.data());
    VkSurfaceFormatKHR fmt = fmts[0];
    for (auto& f : fmts)
        if (f.format==VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            { fmt = f; break; }
    g_SwapFormat = fmt.format;

    VkSwapchainCreateInfoKHR scCI = {};
    scCI.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    scCI.surface = g_Surface; scCI.minImageCount = imgCount;
    scCI.imageFormat = fmt.format; scCI.imageColorSpace = fmt.colorSpace;
    scCI.imageExtent = g_SwapExtent; scCI.imageArrayLayers = 1;
    scCI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    scCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    scCI.preTransform = caps.currentTransform;
    scCI.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    scCI.presentMode = VK_PRESENT_MODE_FIFO_KHR; scCI.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(g_Device, &scCI, nullptr, &g_Swapchain) != VK_SUCCESS) return false;

    uint32_t n = 0; vkGetSwapchainImagesKHR(g_Device, g_Swapchain, &n, nullptr);
    g_SwapImages.resize(n); vkGetSwapchainImagesKHR(g_Device, g_Swapchain, &n, g_SwapImages.data());

    g_SwapViews.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        VkImageViewCreateInfo ivCI = {};
        ivCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivCI.image = g_SwapImages[i]; ivCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivCI.format = g_SwapFormat;
        ivCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vkCreateImageView(g_Device, &ivCI, nullptr, &g_SwapViews[i]);
    }

    if (g_RenderPass == VK_NULL_HANDLE) {
        VkAttachmentDescription att = {};
        att.format = g_SwapFormat; att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sp = {}; sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sp.colorAttachmentCount = 1; sp.pColorAttachments = &ref;
        VkSubpassDependency dep = {};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; dep.srcAccessMask = 0;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo rpCI = {};
        rpCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpCI.attachmentCount = 1; rpCI.pAttachments = &att;
        rpCI.subpassCount = 1; rpCI.pSubpasses = &sp;
        rpCI.dependencyCount = 1; rpCI.pDependencies = &dep;
        vkCreateRenderPass(g_Device, &rpCI, nullptr, &g_RenderPass);
    }

    g_Framebuffers.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        VkFramebufferCreateInfo fbCI = {};
        fbCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCI.renderPass = g_RenderPass; fbCI.attachmentCount = 1;
        fbCI.pAttachments = &g_SwapViews[i];
        fbCI.width = g_SwapExtent.width; fbCI.height = g_SwapExtent.height; fbCI.layers = 1;
        vkCreateFramebuffer(g_Device, &fbCI, nullptr, &g_Framebuffers[i]);
    }

    g_CmdBuffers.resize(n);
    VkCommandBufferAllocateInfo cbAI = {};
    cbAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAI.commandPool = g_CmdPool; cbAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAI.commandBufferCount = n;
    vkAllocateCommandBuffers(g_Device, &cbAI, g_CmdBuffers.data());
    return true;
}

void DestroySwapchain()
{
    if (!g_Device) return;
    if (!g_CmdBuffers.empty()) {
        vkFreeCommandBuffers(g_Device, g_CmdPool, (uint32_t)g_CmdBuffers.size(), g_CmdBuffers.data());
        g_CmdBuffers.clear();
    }
    for (auto fb : g_Framebuffers) vkDestroyFramebuffer(g_Device, fb, nullptr); g_Framebuffers.clear();
    for (auto iv : g_SwapViews)    vkDestroyImageView(g_Device, iv, nullptr);   g_SwapViews.clear();
    g_SwapImages.clear();
    if (g_Swapchain) { vkDestroySwapchainKHR(g_Device, g_Swapchain, nullptr); g_Swapchain = VK_NULL_HANDLE; }
}

void CleanupVulkan()
{
    if (g_FenceInFlight) { vkDestroyFence(g_Device, g_FenceInFlight, nullptr);     g_FenceInFlight = VK_NULL_HANDLE; }
    if (g_SemRenderDone) { vkDestroySemaphore(g_Device, g_SemRenderDone, nullptr); g_SemRenderDone = VK_NULL_HANDLE; }
    if (g_SemImageAvail) { vkDestroySemaphore(g_Device, g_SemImageAvail, nullptr); g_SemImageAvail = VK_NULL_HANDLE; }
    if (g_RenderPass)    { vkDestroyRenderPass(g_Device, g_RenderPass, nullptr);   g_RenderPass    = VK_NULL_HANDLE; }
    if (g_DescPool)      { vkDestroyDescriptorPool(g_Device, g_DescPool, nullptr); g_DescPool      = VK_NULL_HANDLE; }
    if (g_CmdPool)       { vkDestroyCommandPool(g_Device, g_CmdPool, nullptr);     g_CmdPool       = VK_NULL_HANDLE; }
    if (g_Device)        { vkDestroyDevice(g_Device, nullptr);                     g_Device        = VK_NULL_HANDLE; }
    if (g_Surface)       { vkDestroySurfaceKHR(g_Instance, g_Surface, nullptr);   g_Surface       = VK_NULL_HANDLE; }
    if (g_Instance)      { vkDestroyInstance(g_Instance, nullptr);                 g_Instance      = VK_NULL_HANDLE; }
}

// ================================================================
//  Themes
// ================================================================
void ApplyDarkTheme()
{
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 12.f; s.FrameRounding = 8.f; s.PopupRounding = 8.f;
    s.ScrollbarRounding = 12.f; s.GrabRounding = 8.f; s.ChildRounding = 10.f;
    s.WindowBorderSize = 1.f; s.FrameBorderSize = 0.f;
    s.WindowPadding = ImVec2(14,14); s.FramePadding = ImVec2(10,5); s.ItemSpacing = ImVec2(8,6);
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]       = ImVec4(0.08f,0.08f,0.10f,1.f);
    c[ImGuiCol_ChildBg]        = ImVec4(0.10f,0.10f,0.13f,1.f);
    c[ImGuiCol_FrameBg]        = ImVec4(0.14f,0.14f,0.18f,1.f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.20f,0.20f,0.26f,1.f);
    c[ImGuiCol_TitleBgActive]  = ImVec4(0.12f,0.12f,0.16f,1.f);
    c[ImGuiCol_Button]         = ImVec4(0.18f,0.42f,0.78f,1.f);
    c[ImGuiCol_ButtonHovered]  = ImVec4(0.22f,0.52f,0.90f,1.f);
    c[ImGuiCol_ButtonActive]   = ImVec4(0.14f,0.35f,0.68f,1.f);
    c[ImGuiCol_Separator]      = ImVec4(0.20f,0.20f,0.28f,1.f);
    c[ImGuiCol_Text]           = ImVec4(0.92f,0.92f,0.95f,1.f);
    c[ImGuiCol_TextDisabled]   = ImVec4(0.45f,0.45f,0.50f,1.f);
}

void ApplyLightTheme()
{
    ImGui::StyleColorsLight();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 12.f; s.FrameRounding = 8.f; s.PopupRounding = 8.f;
    s.ScrollbarRounding = 12.f; s.GrabRounding = 8.f; s.ChildRounding = 10.f;
    s.WindowBorderSize = 1.f; s.FrameBorderSize = 0.f;
    s.WindowPadding = ImVec2(14,14); s.FramePadding = ImVec2(10,5); s.ItemSpacing = ImVec2(8,6);
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]       = ImVec4(0.94f,0.94f,0.96f,1.f);
    c[ImGuiCol_ChildBg]        = ImVec4(0.98f,0.98f,1.00f,1.f);
    c[ImGuiCol_FrameBg]        = ImVec4(0.88f,0.88f,0.92f,1.f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.80f,0.80f,0.88f,1.f);
    c[ImGuiCol_TitleBgActive]  = ImVec4(0.80f,0.80f,0.90f,1.f);
    c[ImGuiCol_Button]         = ImVec4(0.26f,0.52f,0.96f,1.f);
    c[ImGuiCol_ButtonHovered]  = ImVec4(0.36f,0.62f,1.00f,1.f);
    c[ImGuiCol_ButtonActive]   = ImVec4(0.18f,0.40f,0.80f,1.f);
    c[ImGuiCol_Separator]      = ImVec4(0.75f,0.75f,0.82f,1.f);
    c[ImGuiCol_Text]           = ImVec4(0.10f,0.10f,0.15f,1.f);
    c[ImGuiCol_TextDisabled]   = ImVec4(0.50f,0.50f,0.55f,1.f);
    c[ImGuiCol_Header]         = ImVec4(0.26f,0.52f,0.96f,0.5f);
    c[ImGuiCol_HeaderHovered]  = ImVec4(0.36f,0.62f,1.00f,0.5f);
}

// ================================================================
std::string FormatTimestamp(int64_t ts)
{
    std::time_t t = ts;
    std::tm* tm = std::localtime(&t);
    char buf[16]; std::strftime(buf, sizeof(buf), "%H:%M", tm);
    return buf;
}
