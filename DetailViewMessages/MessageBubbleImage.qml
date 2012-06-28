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

// FIXME: when image to small in height, arrow is cropped
Item {
    id: messageBubble

    property alias imageSource: image.source
    property bool mirrored: false
    property int maximumWidth: 400
    property int maximumHeight: 200

    width: bubble.width
    height: bubble.height

    Item {
        x: bubble.leftPadding - (image.width - image.paintedWidth)/2
        y: bubble.topPadding - (image.height - image.paintedHeight)/2

        Image {
            id: image

            width: messageBubble.maximumWidth - bubble.leftPadding - bubble.rightPadding
            height: messageBubble.maximumHeight - bubble.topPadding - bubble.bottomPadding

            clip: true
            fillMode: Image.PreserveAspectFit
            smooth: true
            cache: false
            asynchronous: true
            source: "../dummydata/fake_mms.jpg"
        }
    }

    BorderImage {
        id: bubble

        property int topPadding: 4
        property int bottomPadding: 6
        property int leftPadding: messageBubble.mirrored ? 6 : 13
        property int rightPadding: messageBubble.mirrored ? 13 : 6

        width: image.paintedWidth + leftPadding + rightPadding
        height: image.paintedHeight + topPadding + bottomPadding
        smooth: true
        source: messageBubble.mirrored ? "../assets/bubble_image_right.png" : "../assets/bubble_image_left.png"
        border {top: 15; bottom: 40; left: messageBubble.mirrored ? 15 : 21; right: messageBubble.mirrored ? 21 : 15}
    }
}