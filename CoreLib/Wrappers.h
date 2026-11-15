#pragma once

#include <functional>
#include <unordered_set>
#include <detours.h>

namespace bg3se {

    template <class T>
    T* ResolveFunctionTrampoline(T* ref)
    {
        auto p = reinterpret_cast<uint8_t *>(ref);
        if (*p == 0xE9) {
            return ResolveFunctionTrampoline(reinterpret_cast<T*>(p + 5 + *(int32_t*)(p + 1)));
        } else {
            return ref;
        }
    }

    void const* ResolveRealFunctionAddress(void const* ptr);

#if !defined(_MSC_VER)
    // Standard C++ has no implicit conversion between function pointers and object pointers
    // (MSVC allows it as an extension), so the conversions the hooking code relies on are spelled out.
    template <class R, class... Args>
    inline void const* ResolveRealFunctionAddress(R (*ptr)(Args...))
    {
        return ResolveRealFunctionAddress(reinterpret_cast<void const*>(ptr));
    }

    // Type-erased function pointer storage, convertible to and from any function pointer type
    struct ErasedFunctionPtr
    {
        void* Ptr{ nullptr };

        inline ErasedFunctionPtr() = default;
        inline ErasedFunctionPtr(std::nullptr_t) {}

        template <class F> requires std::is_function_v<F>
        inline ErasedFunctionPtr(F* fun) : Ptr(reinterpret_cast<void*>(fun)) {}

        template <class F> requires std::is_function_v<F>
        inline explicit operator F* () const { return reinterpret_cast<F*>(Ptr); }

        inline explicit operator bool() const { return Ptr != nullptr; }
    };
#else
    using ErasedFunctionPtr = void*;
#endif

    extern std::unordered_set<void const *> gRegisteredTrampolines;

    template <typename T>
    class WrappedFunction;

    template <typename R, typename... Params>
    class WrappedFunction<R(Params...)>
    {
    public:
        typedef R(*FuncType)(Params...);

        bool IsWrapped() const
        {
            return FuncTrampoline != nullptr;
        }

        void Wrap(HMODULE Module, char const * ProcName, FuncType NewFunction)
        {
            // FIXME - move to BinaryMappings
            FARPROC ExportProc = GetProcAddress(Module, ProcName);
            if (ExportProc == NULL) {
                ERR("Could not locate export '%s'", ProcName);
                return;
            }

            Wrap(ExportProc, NewFunction);
        }

#if !defined(_MSC_VER)
        template <class F> requires std::is_function_v<F>
        void Wrap(F* Function, FuncType NewFunction)
        {
            Wrap(reinterpret_cast<void*>(Function), NewFunction);
        }
#endif

        void Wrap(void * Function, FuncType NewFunction)
        {
            if (IsWrapped()) {
                ERR("[%s] Tried to wrap function multiple times", typeid(*this).name());
                return;
            }

            OriginalFunc = Function;
            TrampolineFunc = Function;
            NewFunc = NewFunction;
            gRegisteredTrampolines.insert((void const*)NewFunction);
            auto status = DetourAttachEx((PVOID *)&TrampolineFunc, (PVOID)NewFunc, (PDETOUR_TRAMPOLINE *)&FuncTrampoline, NULL, NULL);
            if (status != NO_ERROR) {
                ERR("[%s] Detour attach failed on %p", typeid(*this).name(), Function);
                OriginalFunc = nullptr;
                TrampolineFunc = nullptr;
                NewFunc = nullptr;
                FuncTrampoline = nullptr;
            }
        }

        void Unwrap()
        {
            if (IsWrapped()) {
                DWORD result = DetourDetach((PVOID *)&TrampolineFunc, (PVOID)NewFunc);
                if (result != NO_ERROR) {
                    ERR("[%s] DetourDetach failed on %p", typeid(*this).name(), OriginalFunc);
                }

                OriginalFunc = nullptr;
                TrampolineFunc = nullptr;
                NewFunc = nullptr;
                FuncTrampoline = nullptr;
            }
        }

        __forceinline R operator ()(Params... Args) const
        {
            return FuncTrampoline(std::forward<Params>(Args)...);
        }

        __forceinline FuncType GetTrampoline() const
        {
            return FuncTrampoline;
        }

    private:
        void * OriginalFunc{ nullptr };
        void * TrampolineFunc{ nullptr };
        FuncType NewFunc{ nullptr };
        FuncType FuncTrampoline{ nullptr };
    };

    template <class TObject, class TFunc>
    struct MethodPtrHelpers;

    template <class TObject, class R, class... Params>
    struct MethodPtrHelpers<TObject, R (Params...)>
    {
        using MethodType = R (TObject::*)(Params...);
        using FunctionType = R (*)(TObject*, Params...);
        using BaseFunctionType = R (*)(Params...);
        
#if defined(_MSC_VER)
        static_assert(sizeof(MethodType) == sizeof(FunctionType));

        inline static FunctionType ToFunction(MethodType fun)
        {
            return *reinterpret_cast<FunctionType*>(&fun);
        }

        inline static MethodType ToMethod(FunctionType fun)
        {
            return *reinterpret_cast<MethodType*>(&fun);
        }
#else
        // Itanium C++ ABI: a pointer to member function is { ptr, adj }. For non-virtual
        // functions `ptr` is the function address and `this` is passed as the first argument,
        // so the conversion is valid as long as no virtual methods / this-adjustments are involved.
        struct ItaniumMethodPtr
        {
            FunctionType Ptr;
            std::ptrdiff_t Adj;
        };
        static_assert(sizeof(MethodType) == sizeof(ItaniumMethodPtr));

        inline static FunctionType ToFunction(MethodType fun)
        {
            ItaniumMethodPtr repr;
            memcpy(&repr, &fun, sizeof(repr));
            se_assert(repr.Adj == 0);
            return repr.Ptr;
        }

        inline static MethodType ToMethod(FunctionType fun)
        {
            ItaniumMethodPtr repr{ fun, 0 };
            MethodType method;
            memcpy(&method, &repr, sizeof(repr));
            return method;
        }
#endif
    };

    template <class Tag, class T>
    class WrappableFunction;

    template <class Tag, class R, class... Params>
    class WrappableFunction<Tag, R(Params...)>
    {
    public:
        template <class TReturn, class... TParams>
        struct PostHookSignature
        {
            using Type = void (void*, Params..., R);
        };
        
        template <class... TParams>
        struct PostHookSignature<void, TParams...>
        {
            using Type = void (void*, Params...);
        };
        
        template <class TContext, class TReturn, class... TParams>
        struct PostHookCallbackSignature
        {
            using Type = void (TContext::*)(Params..., TReturn);
        };
        
        template <class TContext, class... TParams>
        struct PostHookCallbackSignature<TContext, void, TParams...>
        {
            using Type = void (TContext::*)(Params...);
        };

        using BaseFuncType = R (Params...);
        using HookFuncType = R (void*, BaseFuncType*, Params...);
        using PreHookFuncType = void (void*, Params...);
        using PostHookFuncType = PostHookSignature<R, Params...>::Type;
        using NoContextHookFuncType = R (BaseFuncType*, Params...);

        void Wrap(HMODULE Module, char const * ProcName)
        {
            if (gHook != nullptr) {
                ERR("[%s] Hook already registered", typeid(*this).name());
                return;
            }

            this->wrapped_.Wrap(Module, ProcName, &CallToTrampoline);
            gHook = this;
        }

#if !defined(_MSC_VER)
        template <class F> requires std::is_function_v<F>
        void Wrap(F* Function)
        {
            Wrap(reinterpret_cast<void*>(Function));
        }
#endif

        void Wrap(void * Function)
        {
            if (gHook != nullptr) {
                ERR("[%s] Hook already registered", typeid(*this).name());
            }

            this->wrapped_.Wrap(Function, &CallToTrampoline);

            gHook = this;
        }

        void Unwrap()
        {
            this->wrapped_.Unwrap();
            gHook = nullptr;
        }

        bool IsWrapped() const
        {
            return wrapped_.IsWrapped();
        }

        void SetWrapper(NoContextHookFuncType* wrapper)
        {
            if (hook_ != nullptr) {
                ERR("[%s] Function already wrapped", typeid(*this).name());
                return;
            }

            gRegisteredTrampolines.insert(ResolveRealFunctionAddress(&NoContextHook));
            gRegisteredTrampolines.insert(ResolveRealFunctionAddress(wrapper));
            hook_ = &NoContextHook;
            func_ = wrapper;
            func2_ = nullptr;
            context_ = nullptr;
        }

        template <class TContext>
        void SetWrapper(R (* wrapper)(TContext*, BaseFuncType*, Params...), TContext* context)
        {
            if (hook_ != nullptr) {
                ERR("[%s] Function already wrapped", typeid(*this).name());
                return;
            }

            gRegisteredTrampolines.insert(ResolveRealFunctionAddress(wrapper));
            hook_ = reinterpret_cast<HookFuncType*>(wrapper);
            func_ = nullptr;
            func2_ = nullptr;
            context_ = context;
        }

        template <class TContext>
        void SetWrapper(R (TContext::* wrapper)(BaseFuncType*, Params...), TContext* context)
        {
            if (hook_ != nullptr) {
                ERR("[%s] Function already wrapped", typeid(*this).name());
                return;
            }

            auto fun = MethodPtrHelpers<TContext, R (BaseFuncType*, Params...)>::ToFunction(wrapper);
            gRegisteredTrampolines.insert(ResolveRealFunctionAddress(fun));
            hook_ = reinterpret_cast<HookFuncType*>(fun);
            func_ = nullptr;
            func2_ = nullptr;
            context_ = context;
        }

        template <class TContext>
        void SetPreHook(void (TContext::* wrapper)(Params...), TContext* context)
        {
            if (hook_ != nullptr) {
                ERR("[%s] Function already wrapped", typeid(*this).name());
                return;
            }

            gRegisteredTrampolines.insert(ResolveRealFunctionAddress(&StaticPreHook));
            hook_ = &StaticPreHook;
            func_ = MethodPtrHelpers<TContext, void (Params...)>::ToFunction(wrapper);
            func2_ = nullptr;
            context_ = context;
        }

        template <class TContext>
        void SetPostHook(PostHookCallbackSignature<TContext, R, Params...>::Type wrapper, TContext* context)
        {
            if (hook_ != nullptr) {
                ERR("[%s] Function already wrapped", typeid(*this).name());
                return;
            }

            gRegisteredTrampolines.insert(ResolveRealFunctionAddress(&StaticPostHook));
            hook_ = &StaticPostHook;
            if constexpr (std::is_same_v<R, void>) {
                func_ = MethodPtrHelpers<TContext, void(Params...)>::ToFunction(wrapper);
            } else {
                func_ = MethodPtrHelpers<TContext, void(Params..., R)>::ToFunction(wrapper);
            }
            func2_ = nullptr;
            context_ = context;
        }

        template <class TContext>
        void SetPrePostHook(void (TContext::* preHook)(Params...), PostHookCallbackSignature<TContext, R, Params...>::Type postHook, TContext* context)
        {
            if (hook_ != nullptr) {
                ERR("[%s] Function already wrapped", typeid(*this).name());
                return;
            }

            gRegisteredTrampolines.insert(ResolveRealFunctionAddress(&StaticPrePostHook));
            hook_ = &StaticPrePostHook;
            func_ = MethodPtrHelpers<TContext, void(Params...)>::ToFunction(preHook);
            if constexpr (std::is_same_v<R, void>) {
                func2_ = MethodPtrHelpers<TContext, void(Params...)>::ToFunction(postHook);
            } else {
                func2_ = MethodPtrHelpers<TContext, void(Params..., R)>::ToFunction(postHook);
            }
            context_ = context;
        }

        void ClearHook()
        {
            hook_ = nullptr;
            func_ = nullptr;
            func2_ = nullptr;
            context_ = nullptr;
        }

        inline bool IsHooked() const
        {
            return hook_ != nullptr;
        }

        __forceinline R CallOriginal(Params... Args) const
        {
            return wrapped_(std::forward<Params>(Args)...);
        }

        __forceinline R CallWithHooks(Params... Args) const
        {
            if (hook_) {
                return hook_(context_, this->wrapped_.GetTrampoline(), std::forward<Params>(Args)...);
            } else {
                return this->CallOriginal(std::forward<Params>(Args)...);
            }
        }

    private:
        WrappedFunction<R(Params...)> wrapped_;
        HookFuncType* hook_{ nullptr };
        ErasedFunctionPtr func_{ nullptr };
        ErasedFunctionPtr func2_{ nullptr };
        void* context_{ nullptr };

        static R CallToTrampoline(Params... Args)
        {
            return gHook->CallWithHooks(std::forward<Params>(Args)...);
        }

        static R NoContextHook(void* ctx, BaseFuncType* fun, Params... args)
        {
            auto hook = static_cast<NoContextHookFuncType*>(gHook->func_);
            return hook(fun, std::forward<Params>(args)...);
        }

        static R StaticPreHook(void* ctx, BaseFuncType* fun, Params... args)
        {
            auto hook = static_cast<PreHookFuncType*>(gHook->func_);
            hook(ctx, std::forward<Params>(args)...);
            return gHook->CallOriginal(std::forward<Params>(args)...);
        }

        static R StaticPostHook(void* ctx, BaseFuncType* fun, Params... args)
        {
            auto hook = static_cast<PostHookFuncType*>(gHook->func_);
            if constexpr (std::is_same_v<R, void>) {
                gHook->CallOriginal(std::forward<Params>(args)...);
                hook(ctx, std::forward<Params>(args)...);
            } else {
                auto retval = gHook->CallOriginal(std::forward<Params>(args)...);
                hook(ctx, std::forward<Params>(args)..., retval);
                return retval;
            }
        }

        static R StaticPrePostHook(void* ctx, BaseFuncType* fun, Params... args)
        {
            auto preHook = static_cast<PreHookFuncType*>(gHook->func_);
            preHook(ctx, std::forward<Params>(args)...);

            auto postHook = static_cast<PostHookFuncType*>(gHook->func2_);
            if constexpr (std::is_same_v<R, void>) {
                gHook->CallOriginal(std::forward<Params>(args)...);
                postHook(ctx, std::forward<Params>(args)...);
            } else {
                auto retval = gHook->CallOriginal(std::forward<Params>(args)...);
                postHook(ctx, std::forward<Params>(args)..., retval);
                return retval;
            }
        }

        static WrappableFunction<Tag, R(Params...)> * gHook;
    };

    // Every WrappableFunction instantiation needs storage for its static gHook pointer.
    // MSVC accepts a plain per-hook definition; standard C++ wants it defined once as a template.
#if defined(_MSC_VER)
#define SE_DEFINE_HOOK_STORAGE(...) __VA_ARGS__ * __VA_ARGS__::gHook;
#else
#define SE_DEFINE_HOOK_STORAGE(...)
    template <class Tag, class R, class... Params>
    WrappableFunction<Tag, R(Params...)>* WrappableFunction<Tag, R(Params...)>::gHook{ nullptr };
#endif
}
