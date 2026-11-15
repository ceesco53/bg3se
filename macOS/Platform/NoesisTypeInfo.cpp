// Type information for Noesis classes the extender derives from or names in typeid().
//
// Under the Itanium C++ ABI a polymorphic class's type_info is emitted once, next to its key
// function, i.e. inside the NoesisGUI library. That library is linked statically into the game
// and does not export these symbols, so the extender cannot bind to them (MSVC emits type
// information into every object file, which is why the Windows build never needed this).
// The definitions below stand in for them inside this dylib. They describe each class as a root
// class: dynamic_cast between extender-defined subclasses still works, but the standard RTTI
// view of the Noesis hierarchy above them is not available - use Noesis::DynamicCast for that.
#include <cstddef>

// vtable of abi::__class_type_info (libc++abi)
extern "C" void const* const _ZTVN10__cxxabiv117__class_type_infoE[];

namespace {
struct ClassTypeInfo
{
    void const* VTable;
    char const* Name;
};
}

#define NOESIS_TYPE_INFO(length, cls) \
    extern "C" __attribute__((visibility("hidden"))) const ClassTypeInfo NoesisTypeInfo_##cls \
        __asm__("__ZTIN6Noesis" #length #cls "E"); \
    const ClassTypeInfo NoesisTypeInfo_##cls = { &_ZTVN10__cxxabiv117__class_type_infoE[2], "N6Noesis" #length #cls "E" };

NOESIS_TYPE_INFO(14, DependencyData)
NOESIS_TYPE_INFO(16, DependencyObject)
NOESIS_TYPE_INFO(18, DependencyProperty)
NOESIS_TYPE_INFO(16, DispatcherObject)
NOESIS_TYPE_INFO(16, FrameworkElement)
NOESIS_TYPE_INFO(5, Panel)
NOESIS_TYPE_INFO(11, RoutedEvent)
NOESIS_TYPE_INFO(12, TypeMetaData)
NOESIS_TYPE_INFO(9, UIElement)
NOESIS_TYPE_INFO(13, UIElementData)
NOESIS_TYPE_INFO(6, Visual)
