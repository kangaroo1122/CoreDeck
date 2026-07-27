//
// Created by kangaroo. on 26/07/2026.
//

#import <Cocoa/Cocoa.h>

#include "macos_menu.h"

#include <mutex>
#include <string>
#include <vector>

#include "../gui/localization.h"
#include "../core/shared_folder.h"

namespace {
    std::mutex g_ActionMutex;
    std::vector<CoreDeck::NativeMenuAction> g_Actions;
    bool g_Installed = false;
    bool g_Interactive = true;
    bool g_UpdateCheckInFlight = false;

    NSMenuItem *g_AboutItem = nil;
    NSMenuItem *g_PreferencesItem = nil;
    NSMenuItem *g_QuitItem = nil;
    NSMenuItem *g_ViewMenuItem = nil;
    NSMenuItem *g_ToggleAvdListItem = nil;
    NSMenuItem *g_ToggleOptionsItem = nil;
    NSMenuItem *g_ToggleDetailsItem = nil;
    NSMenuItem *g_ToggleOutputLogItem = nil;
    NSMenuItem *g_ToggleDeviceExplorerItem = nil;
    NSMenuItem *g_StorageOverviewItem = nil;
    NSMenuItem *g_ToolsMenuItem = nil;
    NSMenuItem *g_DeviceExplorerItem = nil;
    NSMenuItem *g_SharedFolderMenuItem = nil;
    NSMenuItem *g_OpenSharedFolderHostItem = nil;
    NSMenuItem *g_OpenSharedFolderEmulatorItem = nil;
    NSMenuItem *g_HelpMenuItem = nil;
    NSMenuItem *g_CheckForUpdatesItem = nil;

    NSString *NsString(const std::string &value) {
        return [NSString stringWithUTF8String:value.c_str()];
    }

    NSString *NsString(const char *value) {
        return [NSString stringWithUTF8String:value == nullptr ? "" : value];
    }

    NSString *Translated(const char *value) {
        return NsString(CoreDeck::Tr(value));
    }

    NSString *QuitTitle() {
        return NsString(std::string(CoreDeck::Tr("Quit")) + " " + COREDECK_TITLE);
    }

    NSString *HideAppTitle() {
        return NsString(std::string("Hide ") + COREDECK_TITLE);
    }

    void PushAction(const CoreDeck::NativeMenuAction action) {
        std::lock_guard lock(g_ActionMutex);
        g_Actions.push_back(action);
    }
}

@interface CoreDeckMenuTarget : NSObject <NSMenuItemValidation>
- (void)dispatchAction:(id)sender;
@end

@implementation CoreDeckMenuTarget
- (void)dispatchAction:(id)sender {
    if (![sender isKindOfClass:[NSMenuItem class]]) {
        return;
    }

    NSMenuItem *item = (NSMenuItem *) sender;
    const auto action = static_cast<CoreDeck::NativeMenuAction>([item tag]);
    PushAction(action);
}

- (BOOL)validateMenuItem:(NSMenuItem *)menuItem {
    const auto action = static_cast<CoreDeck::NativeMenuAction>([menuItem tag]);
    if (action == CoreDeck::NativeMenuAction::Quit) {
        return YES;
    }
    if (!g_Interactive) {
        return NO;
    }
    if (action == CoreDeck::NativeMenuAction::CheckForUpdates) {
        return !g_UpdateCheckInFlight;
    }
    return YES;
}
@end

namespace {
    CoreDeckMenuTarget *g_Target = nil;

    NSMenuItem *ActionItem(
        NSString *title,
        const CoreDeck::NativeMenuAction action,
        NSString *keyEquivalent = @""
    ) {
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title
                                                      action:@selector(dispatchAction:)
                                               keyEquivalent:keyEquivalent];
        [item setTarget:g_Target];
        [item setTag:static_cast<NSInteger>(action)];
        return item;
    }

    void AddServicesMenu(NSMenu *appMenu) {
        NSMenuItem *servicesItem = [[NSMenuItem alloc] initWithTitle:@"Services" action:nil keyEquivalent:@""];
        NSMenu *servicesMenu = [[NSMenu alloc] initWithTitle:@"Services"];
        [servicesItem setSubmenu:servicesMenu];
        [appMenu addItem:servicesItem];
        [NSApp setServicesMenu:servicesMenu];
    }

    void AddStandardHideItems(NSMenu *appMenu) {
        NSMenuItem *hideItem = [[NSMenuItem alloc] initWithTitle:HideAppTitle()
                                                          action:@selector(hide:)
                                                   keyEquivalent:@"h"];
        [hideItem setTarget:NSApp];
        [appMenu addItem:hideItem];

        NSMenuItem *hideOthersItem = [[NSMenuItem alloc] initWithTitle:@"Hide Others"
                                                                 action:@selector(hideOtherApplications:)
                                                          keyEquivalent:@"h"];
        [hideOthersItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagOption];
        [hideOthersItem setTarget:NSApp];
        [appMenu addItem:hideOthersItem];

        NSMenuItem *showAllItem = [[NSMenuItem alloc] initWithTitle:@"Show All"
                                                             action:@selector(unhideAllApplications:)
                                                      keyEquivalent:@""];
        [showAllItem setTarget:NSApp];
        [appMenu addItem:showAllItem];
    }

    void SetMenuTitle(NSMenuItem *item, NSString *title) {
        [item setTitle:title];
        [[item submenu] setTitle:title];
    }

    void SyncItemTitles(const CoreDeck::NativeMenuState &state) {
        g_Interactive = state.Interactive;
        g_UpdateCheckInFlight = state.UpdateCheckInFlight;

        [g_AboutItem setTitle:Translated("About CoreDeck")];
        [g_PreferencesItem setTitle:Translated("Preferences...")];
        [g_QuitItem setTitle:QuitTitle()];

        SetMenuTitle(g_ViewMenuItem, Translated("View"));
        [g_ToggleAvdListItem setTitle:Translated(state.ShowAvdListPanel ? "Hide AVD List" : "Show AVD List")];
        [g_ToggleOptionsItem setTitle:Translated(state.ShowOptionsPanel ? "Hide Options" : "Show Options")];
        [g_ToggleDetailsItem setTitle:Translated(state.ShowDetailsPanel ? "Hide Details" : "Show Details")];
        [g_ToggleOutputLogItem setTitle:Translated(state.ShowLogPanel ? "Hide Output Log" : "Show Output Log")];
        [g_ToggleDeviceExplorerItem setTitle:Translated(state.ShowDeviceExplorerPanel ? "Hide Device Explorer" : "Show Device Explorer")];
        [g_StorageOverviewItem setTitle:Translated("Storage Overview")];

        SetMenuTitle(g_ToolsMenuItem, Translated("Tools"));
        [g_DeviceExplorerItem setTitle:Translated("Device Explorer")];
        SetMenuTitle(g_SharedFolderMenuItem, Translated("Shared Folder"));
        [g_OpenSharedFolderHostItem setTitle:Translated(CoreDeck::GetOpenSharedFolderHostLabel())];
        [g_OpenSharedFolderEmulatorItem setTitle:Translated("Open Shared Folder in Emulator")];
        [g_ToolsMenuItem setHidden:!state.ShowToolsMenu];

        SetMenuTitle(g_HelpMenuItem, Translated("Help"));
        [g_CheckForUpdatesItem setTitle:Translated("Check for Updates...")];

        [g_AboutItem setEnabled:state.Interactive];
        [g_PreferencesItem setEnabled:state.Interactive];
        [g_ViewMenuItem setEnabled:state.Interactive];
        [g_ToggleAvdListItem setEnabled:state.Interactive];
        [g_ToggleOptionsItem setEnabled:state.Interactive];
        [g_ToggleDetailsItem setEnabled:state.Interactive];
        [g_ToggleOutputLogItem setEnabled:state.Interactive];
        [g_ToggleDeviceExplorerItem setEnabled:state.Interactive];
        [g_StorageOverviewItem setEnabled:state.Interactive];
        [g_ToolsMenuItem setEnabled:state.Interactive && state.ShowToolsMenu];
        [g_DeviceExplorerItem setEnabled:state.Interactive && state.ShowToolsMenu];
        [g_SharedFolderMenuItem setEnabled:state.Interactive && state.ShowToolsMenu];
        [g_OpenSharedFolderHostItem setEnabled:state.Interactive && state.ShowToolsMenu];
        [g_OpenSharedFolderEmulatorItem setEnabled:state.Interactive && state.ShowToolsMenu];
        [g_HelpMenuItem setEnabled:state.Interactive];
        [g_CheckForUpdatesItem setEnabled:state.Interactive && !state.UpdateCheckInFlight];
    }
}

namespace CoreDeck::MacosMenu {
    void Install() {
        @autoreleasepool {
            if (g_Installed) {
                return;
            }

            [NSApplication sharedApplication];
            g_Target = [[CoreDeckMenuTarget alloc] init];

            NSMenu *mainMenu = [[NSMenu alloc] initWithTitle:@""];

            NSMenuItem *appMenuItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
            [mainMenu addItem:appMenuItem];

            NSMenu *appMenu = [[NSMenu alloc] initWithTitle:NsString(COREDECK_TITLE)];
            [appMenuItem setSubmenu:appMenu];

            g_AboutItem = ActionItem(Translated("About CoreDeck"), NativeMenuAction::About);
            [appMenu addItem:g_AboutItem];
            [appMenu addItem:[NSMenuItem separatorItem]];

            g_PreferencesItem = ActionItem(Translated("Preferences..."), NativeMenuAction::Preferences, @",");
            [appMenu addItem:g_PreferencesItem];
            [appMenu addItem:[NSMenuItem separatorItem]];

            AddServicesMenu(appMenu);
            [appMenu addItem:[NSMenuItem separatorItem]];
            AddStandardHideItems(appMenu);
            [appMenu addItem:[NSMenuItem separatorItem]];

            g_QuitItem = ActionItem(QuitTitle(), NativeMenuAction::Quit, @"q");
            [appMenu addItem:g_QuitItem];

            g_ViewMenuItem = [[NSMenuItem alloc] initWithTitle:Translated("View") action:nil keyEquivalent:@""];
            [mainMenu addItem:g_ViewMenuItem];
            NSMenu *viewMenu = [[NSMenu alloc] initWithTitle:Translated("View")];
            [g_ViewMenuItem setSubmenu:viewMenu];
            g_ToggleAvdListItem = ActionItem(Translated("Hide AVD List"), NativeMenuAction::ToggleAvdList);
            g_ToggleOptionsItem = ActionItem(Translated("Hide Options"), NativeMenuAction::ToggleOptions);
            g_ToggleDetailsItem = ActionItem(Translated("Hide Details"), NativeMenuAction::ToggleDetails);
            g_ToggleOutputLogItem = ActionItem(Translated("Hide Output Log"), NativeMenuAction::ToggleOutputLog);
            g_ToggleDeviceExplorerItem = ActionItem(Translated("Show Device Explorer"), NativeMenuAction::ToggleDeviceExplorer);
            g_StorageOverviewItem = ActionItem(Translated("Storage Overview"), NativeMenuAction::StorageOverview);
            [viewMenu addItem:g_ToggleAvdListItem];
            [viewMenu addItem:g_ToggleOptionsItem];
            [viewMenu addItem:g_ToggleDetailsItem];
            [viewMenu addItem:g_ToggleOutputLogItem];
            [viewMenu addItem:g_ToggleDeviceExplorerItem];
            [viewMenu addItem:[NSMenuItem separatorItem]];
            [viewMenu addItem:g_StorageOverviewItem];

            g_ToolsMenuItem = [[NSMenuItem alloc] initWithTitle:Translated("Tools") action:nil keyEquivalent:@""];
            [mainMenu addItem:g_ToolsMenuItem];
            NSMenu *toolsMenu = [[NSMenu alloc] initWithTitle:Translated("Tools")];
            [g_ToolsMenuItem setSubmenu:toolsMenu];
            g_DeviceExplorerItem = ActionItem(Translated("Device Explorer"), NativeMenuAction::DeviceExplorer);
            [toolsMenu addItem:g_DeviceExplorerItem];
            g_SharedFolderMenuItem = [[NSMenuItem alloc] initWithTitle:Translated("Shared Folder") action:nil keyEquivalent:@""];
            NSMenu *sharedFolderMenu = [[NSMenu alloc] initWithTitle:Translated("Shared Folder")];
            [g_SharedFolderMenuItem setSubmenu:sharedFolderMenu];
            g_OpenSharedFolderHostItem = ActionItem(
                Translated(CoreDeck::GetOpenSharedFolderHostLabel()),
                NativeMenuAction::OpenSharedFolderHost
            );
            g_OpenSharedFolderEmulatorItem = ActionItem(
                Translated("Open Shared Folder in Emulator"),
                NativeMenuAction::OpenSharedFolderEmulator
            );
            [sharedFolderMenu addItem:g_OpenSharedFolderHostItem];
            [sharedFolderMenu addItem:g_OpenSharedFolderEmulatorItem];
            [toolsMenu addItem:g_SharedFolderMenuItem];

            g_HelpMenuItem = [[NSMenuItem alloc] initWithTitle:Translated("Help") action:nil keyEquivalent:@""];
            [mainMenu addItem:g_HelpMenuItem];
            NSMenu *helpMenu = [[NSMenu alloc] initWithTitle:Translated("Help")];
            [g_HelpMenuItem setSubmenu:helpMenu];
            g_CheckForUpdatesItem = ActionItem(Translated("Check for Updates..."), NativeMenuAction::CheckForUpdates);
            [helpMenu addItem:g_CheckForUpdatesItem];

            [NSApp setMainMenu:mainMenu];
            g_Installed = true;
        }
    }

    void Shutdown() {
        @autoreleasepool {
            g_Installed = false;
            g_Target = nil;
            g_AboutItem = nil;
            g_PreferencesItem = nil;
            g_QuitItem = nil;
            g_ViewMenuItem = nil;
            g_ToggleAvdListItem = nil;
            g_ToggleOptionsItem = nil;
            g_ToggleDetailsItem = nil;
            g_ToggleOutputLogItem = nil;
            g_ToggleDeviceExplorerItem = nil;
            g_StorageOverviewItem = nil;
            g_ToolsMenuItem = nil;
            g_DeviceExplorerItem = nil;
            g_SharedFolderMenuItem = nil;
            g_OpenSharedFolderHostItem = nil;
            g_OpenSharedFolderEmulatorItem = nil;
            g_HelpMenuItem = nil;
            g_CheckForUpdatesItem = nil;
            std::lock_guard lock(g_ActionMutex);
            g_Actions.clear();
        }
    }

    void Update(const NativeMenuState &state) {
        @autoreleasepool {
            if (!g_Installed) {
                Install();
            }
            SyncItemTitles(state);
        }
    }

    std::optional<NativeMenuAction> PollAction() {
        std::lock_guard lock(g_ActionMutex);
        if (g_Actions.empty()) {
            return std::nullopt;
        }

        const NativeMenuAction action = g_Actions.front();
        g_Actions.erase(g_Actions.begin());
        return action;
    }
}
