/*
 * Copyright 2012 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

import QtQuick 1.1
import TelephonyApp 0.1

ScrollbarForFlickable {
    id: scrollbar

    property ListView view

    flickable: view
    /* ListView.contentHeight is not reliable when section headers are defined.
       In that case we compute 'contentSize' manually.

       Ref.: https://bugreports.qt-project.org/browse/QTBUG-17057
             https://bugreports.qt-project.org/browse/QTBUG-19941
    */
    contentSize: {
        if (sectionCounter.sectionCount == 0) {
            // when no section header, ListView.contentHeight is reliable
            return view.contentHeight
        } else {
            return sectionCounter.sectionCount * scrollbar.__sectionHeaderHeight + scrollbar.view.count * scrollbar.__delegateHeight
        }
    }

    /* Compute delegate and section header height by instantiating view.delegate
       and view.section.delegate once.

       WARNING: it assumes that the height of every delegate is the same and the
       height of every section header is the same.
       Qt Quick Components did the same and had bugs reported:
       https://bugreports.qt-project.org/browse/QTCOMPONENTS-1169
    */
    property int __delegateHeight: sectionCounter.sectionCount > 0 ? __getHeightFromComponent(view.delegate) : 0
    property int __sectionHeaderHeight: __getHeightFromComponent(view.section.delegate)

    function __getHeightFromComponent(component) {
        if (component != undefined) {
            var instance = component.createObject(null)
            var height = instance.height
            instance.destroy()
            return height
        } else {
            return 0
        }
    }

    VisualDataModel {
        id: visualModel
        model: view.section.delegate != undefined ? scrollbar.view.model : undefined
        delegate: Item {}
    }

    ModelSectionCounter {
        id: sectionCounter
        model: view.section.delegate != undefined ? visualModel : undefined
        sectionProperty: view.section.property
        sectionCriteria: view.section.criteria
    }
}
