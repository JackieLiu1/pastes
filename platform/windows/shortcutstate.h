#ifndef SHORTCUT_WIN_STATE_H
#define SHORTCUT_WIN_STATE_H

#include <windows.h>

/* Keep only modifier/V state. The hook neither records text nor handles UI. */
class WindowsShortcutState
{
public:
	struct Result { bool consume = false; bool open = false; };

	void seedModifier(unsigned key, bool down)
	{
		if (key < 256) m_down[key] = down;
	}

	Result keyEvent(unsigned key, bool down, bool injected = false)
	{
		Result result;
		if (injected || key >= 256) return result;
		const bool controlKey = key == VK_LCONTROL || key == VK_RCONTROL || key == VK_CONTROL;
		const bool modifier = controlKey || key == VK_LWIN || key == VK_RWIN ||
			key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT ||
			key == VK_MENU || key == VK_LMENU || key == VK_RMENU;
		if (!modifier && key != 'V') return result;
		const bool wasDown = m_down[key];
		m_down[key] = down;
		if (key == 'V') {
			if (down && !wasDown && win() && !ctrl() && !shift() && !alt()) {
				m_consumedV = true;
				result.open = true;
			}
			result.consume = m_consumedV;
			if (!down) m_consumedV = false;
		}
		return result;
	}

private:
	bool ctrl(void) const { return m_down[VK_CONTROL] || m_down[VK_LCONTROL] || m_down[VK_RCONTROL]; }
	bool win(void) const { return m_down[VK_LWIN] || m_down[VK_RWIN]; }
	bool shift(void) const { return m_down[VK_SHIFT] || m_down[VK_LSHIFT] || m_down[VK_RSHIFT]; }
	bool alt(void) const { return m_down[VK_MENU] || m_down[VK_LMENU] || m_down[VK_RMENU]; }
	bool m_down[256] = {};
	bool m_consumedV = false;
};

#endif
