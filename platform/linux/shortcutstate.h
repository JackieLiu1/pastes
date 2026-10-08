#ifndef SHORTCUT_LINUX_STATE_H
#define SHORTCUT_LINUX_STATE_H

#include <X11/X.h>

class LinuxShortcutState
{
public:
	static bool matches(unsigned type, unsigned key, unsigned pasteKey, unsigned modifiers,
		unsigned lockModifiers = LockMask | Mod2Mask)
	{
		/* Super (the Windows key) uses Mod4; lock modifiers do not affect it. */
		return pasteKey && type == KeyPress && key == pasteKey &&
			((modifiers & 0xffU & ~lockModifiers) == Mod4Mask);
	}
	bool update(unsigned type, unsigned key, unsigned pasteKey, unsigned modifiers,
		unsigned lockModifiers = LockMask | Mod2Mask)
	{
		if (!pasteKey || key != pasteKey) return false;
		if (type == KeyRelease) { m_pressed = false; return false; }
		if (!matches(type, key, pasteKey, modifiers, lockModifiers) || m_pressed) return false;
		m_pressed = true;
		return true;
	}

private:
	bool m_pressed = false;
};

#endif
