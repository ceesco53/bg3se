// Cocoa / CoreFoundation backed platform services.
#include "Darwin.h"

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

namespace bg3se::darwin {

bool GetGameBundleVersion(uint32_t& major, uint32_t& minor, uint32_t& revision, uint32_t& build)
{
    @autoreleasepool {
        NSString* version = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleVersion"];
        if (version == nil) return false;

        NSArray<NSString*>* parts = [version componentsSeparatedByString:@"."];
        if (parts.count < 4) return false;

        major = (uint32_t)parts[0].longLongValue;
        minor = (uint32_t)parts[1].longLongValue;
        revision = (uint32_t)parts[2].longLongValue;
        build = (uint32_t)parts[3].longLongValue;
        return true;
    }
}

std::string GetGameExecutableDir()
{
    @autoreleasepool {
        NSString* path = [[NSBundle mainBundle] executablePath];
        return path ? std::string([[path stringByDeletingLastPathComponent] UTF8String]) : std::string();
    }
}

std::string GetExtenderDataDir()
{
    @autoreleasepool {
        NSArray<NSString*>* dirs = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
        NSString* base = dirs.count ? dirs[0] : [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support"];
        NSString* dir = [base stringByAppendingPathComponent:@"BG3ScriptExtender"];
        [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
        return std::string([dir UTF8String]);
    }
}

int EngineStage()
{
    static int stage = [] {
        auto env = getenv("BG3SE_ENGINE");
        if (env == nullptr) return 0;
        auto value = atoi(env);
        return value < 0 ? 0 : (value > 3 ? 3 : value);
    }();
    return stage;
}

void ShowAlert(char const* title, char const* message)
{
    NSString* nsTitle = [NSString stringWithUTF8String:title ? title : ""];
    NSString* nsMessage = [NSString stringWithUTF8String:message ? message : ""];

    void (^show)(void) = ^{
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = nsTitle;
        alert.informativeText = nsMessage;
        alert.alertStyle = NSAlertStyleCritical;
        [alert addButtonWithTitle:@"OK"];
        [alert runModal];
    };

    if ([NSThread isMainThread]) {
        show();
    } else if (NSApp != nil) {
        dispatch_sync(dispatch_get_main_queue(), show);
    } else {
        // No application object yet (very early startup): nothing can present UI
        NSLog(@"[BG3SE] %@: %@", nsTitle, nsMessage);
    }
}

bool IsShiftKeyDown()
{
    return (CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState) & kCGEventFlagMaskShift) != 0;
}

}
