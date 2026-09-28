#include "platform/pastetarget.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>

namespace {

AXUIElementRef focusedAttribute(AXUIElementRef application, CFStringRef attribute)
{
	CFTypeRef value = nullptr;
	if (AXUIElementCopyAttributeValue(application, attribute, &value) != kAXErrorSuccess || !value)
		return nullptr;
	if (CFGetTypeID(value) == AXUIElementGetTypeID())
		return static_cast<AXUIElementRef>(const_cast<void *>(value));
	CFRelease(value);
	return nullptr;
}

bool pastePermission(void)
{
	if (!AXIsProcessTrusted())
		return false;
	if (@available(macOS 10.15, *))
		return CGPreflightPostEventAccess();
	return true;
}

void sendPasteShortcut(pid_t target)
{
	CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStatePrivate);
	if (!source)
		return;
	const CGKeyCode keys[] = {kVK_Command, kVK_ANSI_V, kVK_ANSI_V, kVK_Command};
	CGEventRef events[4] = {};
	bool complete = true;
	for (int i = 0; i < 4; ++i) {
		events[i] = CGEventCreateKeyboardEvent(source, keys[i], i < 2);
		if (events[i])
			CGEventSetFlags(events[i], i < 3 ? kCGEventFlagMaskCommand : 0);
		else
			complete = false;
	}
	/* Allocate the whole sequence before sending any modifier key down. */
	if (complete) {
		for (CGEventRef event : events)
			CGEventPostToPid(target, event);
	}
	for (CGEventRef event : events)
		if (event) CFRelease(event);
	CFRelease(source);
}

}

class PasteTarget::Private
{
public:
	~Private()
	{
		clearTarget();
	}

	void clearTarget(void)
	{
		[application release]; application = nil;
		if (window) CFRelease(window);
		if (element) CFRelease(element);
		window = nullptr; element = nullptr;
	}

	NSRunningApplication *application = nil;
	AXUIElementRef window = nullptr;
	AXUIElementRef element = nullptr;
	QPointer<QWidget> panel;
	QTimer timer;
	QElapsedTimer elapsed;
	bool activationRequested = false;
	bool permissionNotified = false;
};

PasteTarget::PasteTarget(QObject *parent) : QObject(parent),
	m_private(std::make_unique<Private>())
{
	m_private->timer.setInterval(20);
	QObject::connect(&m_private->timer, &QTimer::timeout, this, &PasteTarget::tryPaste);
}

PasteTarget::~PasteTarget() = default;

void PasteTarget::cancel(void)
{
	m_private->timer.stop();
	m_private->panel.clear();
}

void PasteTarget::captureTarget(QWidget *panel)
{
	Q_UNUSED(panel);
	cancel();
	m_private->clearTarget();
	NSRunningApplication *application = NSWorkspace.sharedWorkspace.frontmostApplication;
	if (!application || application.terminated ||
	    application.processIdentifier == NSProcessInfo.processInfo.processIdentifier)
		return;
	m_private->application = [application retain];
	if (pastePermission()) {
		AXUIElementRef target = AXUIElementCreateApplication(application.processIdentifier);
		AXUIElementSetMessagingTimeout(target, 0.15f);
		m_private->window = focusedAttribute(target, kAXFocusedWindowAttribute);
		m_private->element = focusedAttribute(target, kAXFocusedUIElementAttribute);
		if (m_private->element) AXUIElementSetMessagingTimeout(m_private->element, 0.15f);
		CFRelease(target);
	}
}

void PasteTarget::notifyPermissionRequired(void)
{
	if (m_private->permissionNotified)
		return;
	m_private->permissionNotified = true;
	emit permissionRequired();
}

void PasteTarget::paste(QWidget *panel, bool hasUrls)
{
	Q_UNUSED(hasUrls);
	cancel();
	if (!m_private->application || m_private->application.terminated)
		return;
	if (!pastePermission()) {
		notifyPermissionRequired();
		return;
	}
	m_private->panel = panel;
	m_private->activationRequested = false;
	m_private->elapsed.start();
	m_private->timer.start();
}

void PasteTarget::tryPaste(void)
{
	NSRunningApplication *target = m_private->application;
	if (!m_private->panel || !target || target.terminated ||
	    m_private->elapsed.elapsed() > 1500) {
		cancel();
		return;
	}
	/* The sliding panel must release key focus before sending the paste. */
	if (m_private->panel->isVisible())
		return;
	const pid_t foreground = NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
	const pid_t own = NSProcessInfo.processInfo.processIdentifier;
	if (foreground != target.processIdentifier && foreground != own) {
		/* A user-initiated application switch cancels the pending paste. */
		cancel();
		return;
	}
	if (!m_private->activationRequested) {
		m_private->activationRequested = true;
		if (!target.active) {
			if (@available(macOS 14.0, *)) {
				[NSApp yieldActivationToApplication:target];
				[target activateFromApplication:NSRunningApplication.currentApplication options:0];
			} else {
				[target activateWithOptions:0];
			}
		}
	}
	if (!target.active || foreground != target.processIdentifier)
		return;
	if (!pastePermission()) {
		cancel();
		notifyPermissionRequired();
		return;
	}
	AXUIElementRef application = AXUIElementCreateApplication(target.processIdentifier);
	AXUIElementSetMessagingTimeout(application, 0.15f);
	AXUIElementRef window = focusedAttribute(application, kAXFocusedWindowAttribute);
	AXUIElementRef element = focusedAttribute(application, kAXFocusedUIElementAttribute);
	CFRelease(application);
	const bool sameWindow = !m_private->window || (window && CFEqual(window, m_private->window));
	const bool sameElement = !m_private->element || (element && CFEqual(element, m_private->element));
	const bool focusPending = (m_private->window && !window) || (m_private->element && !element);
	if (window) CFRelease(window);
	if (element) CFRelease(element);
	if (focusPending)
		return;
	if (!sameWindow) {
		cancel();
		return;
	}
	if (!sameElement) {
		Boolean settable = false;
		if (AXUIElementIsAttributeSettable(m_private->element, kAXFocusedAttribute, &settable) == kAXErrorSuccess &&
		    settable && AXUIElementSetAttributeValue(m_private->element, kAXFocusedAttribute, kCFBooleanTrue) == kAXErrorSuccess)
			return; // Allow AppKit to restore the original responder first.
		cancel();
		return;
	}
	cancel();
	sendPasteShortcut(target.processIdentifier);
}

void PasteTarget::requestPermission(void)
{
	const NSDictionary *options = @{(__bridge NSString *)kAXTrustedCheckOptionPrompt: @YES};
	if (AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options)) {
		if (@available(macOS 10.15, *))
			if (!CGPreflightPostEventAccess()) CGRequestPostEventAccess();
	}
}
