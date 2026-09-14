// Package resources ports resourcepathmanager.hpp and the localization bits of languagemanager.hpp: it
// resolves file paths under the mod's "resources/data" directory tree, and picks which localized
// subdirectory (e.g. "en", "ru") to read props/skills data from.
package resources

import (
	"os"
	"path/filepath"
)

const defaultLocale = "en"

// Manager resolves paths under a resources directory, mirroring ResourcePathManager + LanguageManager.
type Manager struct {
	ResourcesPath string
	Locale        string // requested locale, e.g. "en" or "ru"
}

// NewManager creates a Manager for the given resources directory root (the directory that contains
// "data/", i.e. the parent of resources/data/...), and the requested locale.
func NewManager(resourcesPath, locale string) *Manager {
	if locale == "" {
		locale = defaultLocale
	}
	return &Manager{ResourcesPath: resourcesPath, Locale: locale}
}

// ModLocalization returns the locale to actually use: the requested one if resources/data/<locale> exists
// on disk, otherwise the default ("en").
func (m *Manager) ModLocalization() string {
	if info, err := os.Stat(m.DataPath(m.Locale)); err == nil && info.IsDir() {
		return m.Locale
	}
	return defaultLocale
}

// DataPath returns resources/data/<fileName>.
func (m *Manager) DataPath(fileName string) string {
	return filepath.Join(m.ResourcesPath, "data", fileName)
}

// LocalizedPath returns resources/data/<locale>/<fileName>.dat, using the resolved localization.
func (m *Manager) LocalizedPath(fileName string) string {
	return m.DataPath(filepath.Join(m.ModLocalization(), fileName+".dat"))
}
