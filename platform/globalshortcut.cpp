#include "platform/globalshortcut.h"
#include "platform/shortcut_p.h"

GlobalShortcut::GlobalShortcut(QObject *parent) : QObject(parent),
	m_shortcut(std::make_unique<ShortcutPrivate>())
{
	QObject::connect(this->m_shortcut.get(), &ShortcutPrivate::pasteActivated,
			 this, &GlobalShortcut::pasteActivated);
	QObject::connect(this->m_shortcut.get(), &ShortcutPrivate::primaryShortcutChanged,
			 this, &GlobalShortcut::primaryShortcutChanged);
	this->m_shortcut->start();
}

GlobalShortcut::~GlobalShortcut() = default;
