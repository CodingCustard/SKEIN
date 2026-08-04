include_guard(GLOBAL)

include(FetchContent)

function(skein_configure_dependencies)
    # SDL is the platform windowing backend. Keep it static so every SKEIN
    # executable has the same deployment shape in all four build presets.
    set(SDL_SHARED OFF CACHE BOOL "Build the SDL shared library" FORCE)
    set(SDL_STATIC ON CACHE BOOL "Build the SDL static library" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build the SDL test library" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "Build the SDL tests" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "Build the SDL examples" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "Install SDL" FORCE)
    set(SDL_UNINSTALL OFF CACHE BOOL "Create the SDL uninstall target" FORCE)

    FetchContent_Declare(
        SDL3
        URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.10/SDL3-3.4.10.tar.gz
        URL_HASH SHA256=12b34280415ec8418c864408b93d008a20a6530687ee613d60bfbd20411f2785
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )

    FetchContent_MakeAvailable(SDL3)
endfunction()
