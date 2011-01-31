/*
 * Copyright 2011 Sven Weidauer <sven.weidauer@gmail.com>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#import "BrowserWindowController.h"

#import "BrowserViewController.h"
#import "PSMTabBarControl.h"
#import "PSMRolloverButton.h"
#import "URLFieldCell.h"
#import "cocoa/gui.h"

#import "desktop/browser.h"
#import "desktop/options.h"

@interface BrowserWindowController ()

- (void) canCloseAlertDidEnd:(NSAlert *)alert returnCode:(int)returnCode contextInfo:(void *)contextInfo;

@end


@implementation BrowserWindowController

@synthesize tabBar;
@synthesize tabView;
@synthesize urlField;
@synthesize navigationControl;

@synthesize activeBrowser;
@synthesize activeBrowserController;

- (id) init;
{
	if (nil == (self = [super initWithWindowNibName: @"BrowserWindow"])) return nil;
	
	return self;
}

- (void) dealloc;
{
	[self setTabBar: nil];
	[self setTabView: nil];
	[self setUrlField: nil];
	[self setNavigationControl: nil];
	
	[super dealloc];
}

- (void) awakeFromNib;
{
	[tabBar setShowAddTabButton: YES];
	[tabBar setTearOffStyle: PSMTabBarTearOffMiniwindow];
	[tabBar setCanCloseOnlyTab: YES];
	
	NSButton *b = [tabBar addTabButton];
	[b setTarget: self];
	[b setAction: @selector(newTab:)];
	
	[[self window] setAcceptsMouseMovedEvents: YES];
	
	[urlField setRefreshAction: @selector(reloadPage:)];
	[urlField bind: @"favicon" toObject: activeBrowserController withKeyPath: @"selection.favicon"  options: nil];
	
	[self bind: @"canGoBack" 
	  toObject: activeBrowserController withKeyPath: @"selection.canGoBack" 
	   options: nil];
	[self bind: @"canGoForward" 
	  toObject: activeBrowserController withKeyPath: @"selection.canGoForward" 
	   options: nil];
}

- (void) addTab: (BrowserViewController *)browser;
{
	NSTabViewItem *item = [[[NSTabViewItem alloc] initWithIdentifier: browser] autorelease];
	
	[item setView: [browser view]];
	[item bind: @"label" toObject: browser withKeyPath: @"title" options: nil];
	
	[tabView addTabViewItem: item];
	[browser setWindowController: self];
	
	[tabView selectTabViewItem: item];
}

- (void) removeTab: (BrowserViewController *)browser;
{
	NSUInteger itemIndex = [tabView indexOfTabViewItemWithIdentifier: browser];
	if (itemIndex != NSNotFound) {
		NSTabViewItem *item = [tabView tabViewItemAtIndex: itemIndex];
		[tabView removeTabViewItem: item];
		[browser setWindowController: nil];
	}
}

- (BOOL) windowShouldClose: (NSWindow *) window;
{
	if ([tabView numberOfTabViewItems] <= 1) return YES;
	if ([[NSUserDefaults standardUserDefaults] boolForKey: kAlwaysCloseMultipleTabs]) return YES;
	
	NSAlert *ask = [NSAlert alertWithMessageText: @"Do you really want to close this window?" defaultButton:@"Yes" alternateButton:@"No" otherButton:nil 
					   informativeTextWithFormat: @"There are %d tabs open, do you want to close them all?", [tabView numberOfTabViewItems]];
	[ask setShowsSuppressionButton:YES];
	
	[ask beginSheetModalForWindow: window modalDelegate:self didEndSelector:@selector(canCloseAlertDidEnd:returnCode:contextInfo:) 
					  contextInfo: NULL];

	return NO;
}

- (void) canCloseAlertDidEnd:(NSAlert *)alert returnCode:(int)returnCode contextInfo:(void *)contextInfo;
{
	if (returnCode == NSOKButton) {
		[[NSUserDefaults standardUserDefaults] setBool: [[alert suppressionButton] state] == NSOnState 
												forKey: kAlwaysCloseMultipleTabs];
		[[self window] close];
	}
}

- (void) windowWillClose: (NSNotification *)notification;
{
	for (NSTabViewItem *tab in [tabView tabViewItems]) {
		[tabView removeTabViewItem: tab];
	}
}

- (IBAction) newTab: (id) sender;
{
	browser_window_create( option_homepage_url, [activeBrowser browser], NULL, false, true );
}

- (IBAction) closeCurrentTab: (id) sender;
{
	[self removeTab: activeBrowser];
}

- (void) setActiveBrowser: (BrowserViewController *)newBrowser;
{
	activeBrowser = newBrowser;
	[self setNextResponder: activeBrowser];
}

- (void) setCanGoBack: (BOOL)can;
{
	[navigationControl setEnabled: can forSegment: 0];
}

- (BOOL) canGoBack;
{
	return [navigationControl isEnabledForSegment: 0];
}

- (void) setCanGoForward: (BOOL)can;
{
	[navigationControl setEnabled: can forSegment: 1];
}

- (BOOL) canGoForward;
{
	return [navigationControl isEnabledForSegment: 1];
}

#pragma mark -
#pragma mark Tab bar delegate

- (void) tabView: (NSTabView *)tabView didSelectTabViewItem: (NSTabViewItem *)tabViewItem;
{
	[self setActiveBrowser: [tabViewItem identifier]];
}

- (BOOL)tabView:(NSTabView*)aTabView shouldDragTabViewItem:(NSTabViewItem *)tabViewItem fromTabBar:(PSMTabBarControl *)tabBarControl
{
    return YES;
}

- (BOOL)tabView:(NSTabView*)aTabView shouldDropTabViewItem:(NSTabViewItem *)tabViewItem inTabBar:(PSMTabBarControl *)tabBarControl
{
	[[tabViewItem identifier] setWindowController: self];
	return YES;
}

- (PSMTabBarControl *)tabView:(NSTabView *)aTabView newTabBarForDraggedTabViewItem:(NSTabViewItem *)tabViewItem atPoint:(NSPoint)point;
{
	BrowserWindowController *newWindow = [[[BrowserWindowController alloc] init] autorelease];
	[[tabViewItem identifier] setWindowController: newWindow];
	[[newWindow window] setFrameOrigin: point];
	return newWindow->tabBar;
}

- (void) tabView: (NSTabView *)aTabView didCloseTabViewItem: (NSTabViewItem *)tabViewItem;
{
	[tabViewItem unbind: @"label"];
	
	if (activeBrowser == [tabViewItem identifier]) {
		[self setActiveBrowser: nil];
	}
	
	browser_window_destroy( [[tabViewItem identifier] browser] );
}

@end
