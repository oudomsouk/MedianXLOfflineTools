target_sources(MedianXLOfflineTools PRIVATE
	application.cpp
	application.h
	characterinfo.hpp
	colorsmanager.cpp
	colorsmanager.h
	enums.cpp
	enums.h
	experienceindicatorgroupbox.cpp
	experienceindicatorgroupbox.h
	fileassociationmanager.h
	helpers.cpp
	helpers.h
	helpwindowdisplaymanager.cpp
	helpwindowdisplaymanager.h
	itemdatabase.cpp
	itemdatabase.h
	kexpandablegroupbox.cpp
	kexpandablegroupbox.h
	languagemanager.hpp
	main.cpp
	medianxlofflinetools.cpp
	medianxlofflinetools.h
	medianxlofflinetools.ui
	messagecheckbox.h
	progressbarmodal.hpp
	qd2charrenamer.cpp
	qd2charrenamer.h
	qd2charrenamer.ui
	resourcepathmanager.hpp
	resurrectpenaltydialog.cpp
	resurrectpenaltydialog.h
	resurrectpenaltydialog.ui
	reversebitreader.cpp
	reversebitreader.h
	skilltreedialog.cpp
	skilltreedialog.h
	structs.h
)

if(WIN32)
	target_sources(MedianXLOfflineTools PRIVATE
		fileassociationmanager_win.cpp
		medianxlofflinetools_win.cpp
		messagecheckbox_p.cpp
		windowsincludes.h
	)
	target_link_libraries(MedianXLOfflineTools PRIVATE
		ole32 # COM shit
		shell32 # SHChangeNotify()
		user32 # AllowSetForegroundWindow()
	)
elseif(APPLE)
	target_sources(MedianXLOfflineTools PRIVATE
		application_mac.mm
		fileassociationmanager_mac.mm
		machelpers.h
		machelpers.mm
		medianxlofflinetools_mac.mm
		messagecheckbox_mac_p.mm
	)
	target_link_libraries(MedianXLOfflineTools PRIVATE
		"-framework ApplicationServices" # LSGetApplicationForInfo()
		"-framework AppKit" # NSWindow calls to disable Lion window restoration and NSAlert
	)
else()
	target_sources(MedianXLOfflineTools PRIVATE
		messagecheckbox_p.cpp
	)
endif()
