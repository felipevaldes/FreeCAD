/***************************************************************************
 *   Copyright (c) 2002 Jürgen Riegel <juergen.riegel@web.de>              *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/



#ifndef GUI_DOCKWND_SELECTIONVIEW_H
#define GUI_DOCKWND_SELECTIONVIEW_H


#include <QMenu>
#include <QTimer>
#include "DockWindow.h"
#include "Selection.h"

class QTreeWidget;
class QTreeWidgetItem;
class QCheckBox;
class QLabel;

namespace App {
class DocumentObject;
class SubObjectT;
}

namespace Gui {
namespace DockWnd {

/** A test class. A more elaborate class description.
 */
class SelectionView : public Gui::DockWindow, 
                      public Gui::SelectionObserver
{
    Q_OBJECT

public:
    /**
     * A constructor.
     * A more elaborate description of the constructor.
     */
    SelectionView(Gui::Document* pcDocument, QWidget *parent=0);

    /**
     * A destructor.
     * A more elaborate description of the destructor.
    */
    virtual ~SelectionView();

    /// Observer message from the Selection
    virtual void onSelectionChanged(const SelectionChanges& msg) override;

    virtual void leaveEvent(QEvent*) override;

    bool onMsg(const char* pMsg,const char** ppReturn) override;

    virtual const char *getName(void) const override {return "SelectionView";}

    /// get called when the document is changed or updated
    virtual void onUpdate(void) override;

    QTreeWidget* selectionView;
    QLabel*      countLabel;

    QCheckBox *enablePickList;
    QTreeWidget *pickList;

public Q_SLOTS:
    /// get called when text is entered in the search box
    void search(const QString& text);
    /// get called when enter is pressed in the search box
    void validateSearch(void);
    /// get called when the list is right-clicked
    void onItemContextMenu(const QPoint& point);
    /// different actions
    void select(QTreeWidgetItem* item=0);
    void deselect(void);
    void zoom(void);
    void treeSelect(void);
    void toPython(void);
    void touch(void);
    void showPart(void);
    void onEnablePickList();
    void toggleSelect(QTreeWidgetItem* item=0);
    void preselect(QTreeWidgetItem* item=0);

protected:
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;

private:
    float x,y,z;
    std::vector<App::DocumentObject*> searchList;
};

} // namespace DockWnd

class GuiExport SelectionMenu : public QMenu {
    Q_OBJECT
public:
    SelectionMenu(QWidget *parent=0);
    void doPick(const std::vector<App::SubObjectT> &sels);

public Q_SLOTS:
    void onHover(QAction *);
    void onSubMenu();
    void onTimer();
    void leaveEvent(QEvent *e);
    void beforeShow();

private:
    const std::vector<App::SubObjectT> *pSelList;
    QTimer timer;
    int tooltipIndex;
};


class GuiExport SelUpMenu : public QMenu
{
    Q_OBJECT
public:
    SelUpMenu(QWidget *parent, bool trigger=true);

protected:
    void mouseReleaseEvent(QMouseEvent *e);
    void mousePressEvent(QMouseEvent *e);

protected Q_SLOTS:
    void onTriggered(QAction *action);
    void onHovered(QAction *action);
};

} // namespace Gui

#endif // GUI_DOCKWND_SELECTIONVIEW_H
