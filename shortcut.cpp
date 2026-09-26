#include "shortcut.h"

GlobalShortcut::GlobalShortcut(QObject *parent) : QObject(parent)
{
	this->m_shortcut = new ShortcutPrivate();

	QObject::connect(this->m_shortcut, &ShortcutPrivate::pasteActivated,
			 this, &GlobalShortcut::pasteActivated);
	QObject::connect(this->m_shortcut, &ShortcutPrivate::primaryShortcutChanged,
			 this, &GlobalShortcut::primaryShortcutChanged);
	this->m_shortcut->start();
}

GlobalShortcut::~GlobalShortcut()
{
	delete this->m_shortcut;
}
