/* -*- Mode: C++; indent-tabs-mode: nil; tab-width: 4 -*-
 * -*- coding: utf-8 -*-
 *
 * Copyright (C) 2011 ~ 2017 Deepin, Inc.
 *               2011 ~ 2017 Wang Yong
 *
 * Author:     Wang Yong <wangyong@deepin.com>
 * Maintainer: Wang Yong <wangyong@deepin.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "dwindowmanager.h"

#include <QObject>
#include <QDebug>
#include <QRect>
#include <QtX11Extras/QX11Info>

#include <DObjectPrivate>

#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>

DWM_BEGIN_NAMESPACE

class DWindowManagerPrivate : public DTK_CORE_NAMESPACE::DObjectPrivate
{
public:
    DWindowManagerPrivate(DWindowManager* parent)
        : DObjectPrivate(parent) {}

    mutable xcb_connection_t* conn;
    mutable WindowRect rootWindowRect;
};

DWindowManager::DWindowManager(QObject *parent)
    : QObject(parent),
      DObject(*new DWindowManagerPrivate(this))
{
    D_DC(DWindowManager);
    int screenNum;
    d->conn = xcb_connect(0, &screenNum);
    xcb_screen_t* screen = xcb_aux_get_screen(d->conn, screenNum);
    rootWindow = screen->root;
}

DWindowManager::~DWindowManager()
{
    D_DC(DWindowManager);
    xcb_disconnect(d->conn);
    d->conn = NULL;
}

xcb_atom_t DWindowManager::getAtom(QString name)
{
    D_DC(DWindowManager);
    QByteArray rawName = name.toLatin1();
    xcb_atom_t result = XCB_ATOM_NONE;
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(d->conn, 0, rawName.size(), rawName.data());
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(d->conn, cookie, NULL);
    if(reply) {
        result = reply->atom;

        free(reply);
    }

    return result;
}

xcb_get_property_reply_t* DWindowManager::getProperty(xcb_window_t window, QString propertyName, xcb_atom_t type)
{
    D_DC(DWindowManager);
    xcb_get_property_cookie_t cookie = xcb_get_property(d->conn, 0, window, getAtom(propertyName), type, 0, UINT32_MAX);
    return xcb_get_property_reply(d->conn, cookie, NULL);
}

QString DWindowManager::getAtomName(xcb_atom_t atom)
{
    D_DC(DWindowManager);
    QString result;

    xcb_get_atom_name_cookie_t cookie = xcb_get_atom_name(d->conn, atom);
    xcb_get_atom_name_reply_t *reply = xcb_get_atom_name_reply(d->conn, cookie, NULL);

    if (reply) {
        result = QString::fromLatin1(xcb_get_atom_name_name(reply), xcb_get_atom_name_name_length(reply));
        free(reply);
    }

    return result;
}

QStringList DWindowManager::getWindowTypes(xcb_window_t window)
{
    QStringList types;
    xcb_get_property_reply_t *reply = getProperty(window, "_NET_WM_WINDOW_TYPE", XCB_ATOM_ATOM);

    if(reply) {
        xcb_atom_t *typesA = static_cast<xcb_atom_t*>(xcb_get_property_value(reply));
        int typeNum = reply->length;

        for(int i = 0; i < typeNum; i++) {
            types.append(getAtomName(typesA[i]));
        }

        free(reply);
    }

    return types;
}

QStringList DWindowManager::getWindowStates(xcb_window_t window)
{
    QStringList types;
    xcb_get_property_reply_t *reply = getProperty(window, "_NET_WM_STATE", XCB_ATOM_ATOM);

    if(reply) {
        xcb_atom_t *typesA = static_cast<xcb_atom_t*>(xcb_get_property_value(reply));
        int typeNum = reply->length;

        for(int i = 0; i < typeNum; i++) {
            types.append(getAtomName(typesA[i]));
        }

        free(reply);
    }

    return types;
}

QList<int> DWindowManager::getWindowFrameExtents(xcb_window_t window)
{
    QList<int> extents;

    if (window != rootWindow) {
        xcb_get_property_reply_t *gtkFrameReply = getProperty(window, "_GTK_FRAME_EXTENTS", XCB_ATOM_CARDINAL);

        if (gtkFrameReply) {
            // Because XCB haven't function to check property is exist,
            // we will got wrong value if property '_GTK_FRAME_EXTENTS' is not exist in window xprop attributes.
            // So we check reply->format, if it equal 16 or 32, '_GTK_FRAME_EXTENTS' property is exist.
            if (gtkFrameReply->format == 32 || gtkFrameReply->format == 16) {
                int32_t *value = (int32_t *)xcb_get_property_value(gtkFrameReply);
                for (int i = 0; i < 4; ++i) {
                    extents.append(value[i]);
                }
            } else {
                for (int j = 0; j < 4; ++j) {
                    extents.append(0);
                }
            }
        }

        free(gtkFrameReply);
    }

    return extents;
}

QString DWindowManager::getWindowClass(xcb_window_t window)
{
    if (window == rootWindow) {
        return tr("Desktop");
    } else {
        xcb_get_property_reply_t *reply = getProperty(window, "WM_CLASS", getAtom("STRING"));

        if(reply) {
            QList<QByteArray> rawClasses = QByteArray(static_cast<char*>(xcb_get_property_value(reply)),
                                                      xcb_get_property_value_length(reply)).split('\0');

            free(reply);

            return QString::fromLatin1(rawClasses[0]);
        } else {
            return QString();
        }
    }
}

QString DWindowManager::getWindowName(xcb_window_t window)
{
    if (window == rootWindow) {
        return tr("Desktop");
    } else {
        xcb_get_property_reply_t *reply = getProperty(window, "_NET_WM_NAME", getAtom("UTF8_STRING"));

        if(reply) {
            QString result = QString::fromUtf8(static_cast<char*>(xcb_get_property_value(reply)), xcb_get_property_value_length(reply));

            free(reply);

            return result;
        } else {
            return QString();
        }
    }
}

int DWindowManager::getCurrentWorkspace(xcb_window_t window)
{
    xcb_get_property_reply_t *reply = getProperty(window, "_NET_CURRENT_DESKTOP", XCB_ATOM_CARDINAL);
    int desktop = 0;

    if (reply) {
        desktop = *((int *) xcb_get_property_value(reply));

        free(reply);
    }

    return desktop;
}

int DWindowManager::getWindowWorkspace(xcb_window_t window)
{
    if (window == rootWindow) {
        return getCurrentWorkspace(rootWindow);
    } else {
        xcb_get_property_reply_t *reply = getProperty(window, "_NET_WM_DESKTOP", XCB_ATOM_CARDINAL);
        int desktop = 0;

        if (reply) {
            desktop = *((int *) xcb_get_property_value(reply));

            free(reply);
        }

        return desktop;
    }
}

QList<xcb_window_t> DWindowManager::getWindows()
{
    QList<xcb_window_t> windows;
    xcb_get_property_reply_t *listReply = getProperty(rootWindow, "_NET_CLIENT_LIST_STACKING", XCB_ATOM_WINDOW);

    if (listReply) {
        xcb_window_t *windowList = static_cast<xcb_window_t*>(xcb_get_property_value(listReply));
        int windowListLength = listReply->length;

        for (int i = 0; i < windowListLength; i++) {
            xcb_window_t window = windowList[i];

            foreach(QString type, getWindowTypes(window)) {
                if (type == "_NET_WM_WINDOW_TYPE_NORMAL" ||
                    type == "_NET_WM_WINDOW_TYPE_DIALOG"
                    ) {
                    bool needAppend = false;

                    QStringList states = getWindowStates(window);
                    if (states.length() == 0 ||
                        (!states.contains("_NET_WM_STATE_HIDDEN"))) {
                        if (getWindowWorkspace(window) == getCurrentWorkspace(rootWindow)) {
                            needAppend = true;
                        }
                    }

                    if (needAppend) {
                        windows.append(window);
                        break;
                    }
                }
            }
        }

        free(listReply);
    }

    // We need re-sort windows list from up to bottom,
    // to make compare cursor with window area from up to bottom.
    std::reverse(windows.begin(), windows.end());

    // Add desktop window.
    windows.append(rootWindow);

    // Just use for debug.
    // foreach (auto window, windows) {
    //     qDebug() << getWindowName(window);
    // }

    return windows;
}

void DWindowManager::setRootWindowRect(QRect rect) {
    D_DC(DWindowManager);
    d->rootWindowRect.x = rect.x();
    d->rootWindowRect.y = rect.y();
    d->rootWindowRect.width = rect.width();
    d->rootWindowRect.height = rect.height();
}

WindowRect DWindowManager::getRootWindowRect() {
    D_DC(DWindowManager);
//    WindowRect rect;
//    xcb_get_geometry_reply_t *geometry = xcb_get_geometry_reply(conn, xcb_get_geometry(conn, rootWindow), 0);

//    rect.x = 0;
//    rect.y = 0;
//    rect.width = geometry->width;
//    rect.height = geometry->height;

//    free(geometry);

//    return rect;
    return d->rootWindowRect;
}

void DWindowManager::translateCoords(xcb_window_t window, int32_t& x, int32_t& y)
{
    D_DC(DWindowManager);
    xcb_translate_coordinates_cookie_t c = xcb_translate_coordinates(d->conn,
                                                                     rootWindow, window, x, y);
    xcb_translate_coordinates_reply_t* reply = xcb_translate_coordinates_reply(d->conn, c, NULL);

    if (reply) {
        x = reply->dst_x;
        y = reply->dst_y;

        free(reply);
    }
}

WindowRect DWindowManager::getWindowRect(xcb_window_t window)
{
    D_DC(DWindowManager);
    WindowRect rect;

    xcb_get_geometry_reply_t *geometry = xcb_get_geometry_reply(d->conn,
                                                                xcb_get_geometry(d->conn, window), 0);
    xcb_translate_coordinates_reply_t *coordinate = xcb_translate_coordinates_reply(d->conn,
        xcb_translate_coordinates(d->conn, window, rootWindow, 0, 0), 0);

    QList<int> extents = getWindowFrameExtents(window);

    rect.x = coordinate->dst_x;
    rect.y = coordinate->dst_y;
    rect.width = geometry->width;
    rect.height = geometry->height;

    if (extents.length() == 4) {
        // _GTK_FRAME_EXTENTS: left, right, top, bottom
        rect.x += extents[0];
        rect.y += extents[2];
        rect.width -= extents[0] + extents[1];
        rect.height -= extents[2] + extents[3];
    }

    free(geometry);
    free(coordinate);

    return rect;
}

WindowRect DWindowManager::adjustRectInScreenArea(WindowRect rect)
{
    WindowRect newRect;
    newRect.x = rect.x >= 0 ? rect.x : 0;
    newRect.y = rect.y >= 0 ? rect.y : 0;
    newRect.width = rect.x >= 0 ? rect.width : rect.width + rect.x;
    newRect.height = rect.y >= 0 ? rect.height : rect.height + rect.y;

    WindowRect rootWindowRect = getRootWindowRect();

    if (newRect.x + newRect.width > rootWindowRect.width) {
        newRect.width = rootWindowRect.width - newRect.x;
    }

    if (newRect.y + newRect.height > rootWindowRect.height) {
        newRect.height = rootWindowRect.height - newRect.y;
    }

    return newRect;
}

template <typename... ArgTypes, typename... ArgTypes2>
static inline unsigned int XcbCallVoid(xcb_void_cookie_t (*func)(xcb_connection_t *, ArgTypes...), ArgTypes2... args...)
{
    return func(QX11Info::connection(), args...).sequence;
}

void DWindowManager::setWindowBlur(int wid, QVector<uint32_t> &data)
{
    D_DC(DWindowManager);
    xcb_atom_t atom = getAtom("_NET_WM_DEEPIN_BLUR_REGION_ROUNDED");
    XcbCallVoid(
        xcb_change_property,
        XCB_PROP_MODE_REPLACE,
        wid,
        atom,
        XCB_ATOM_CARDINAL,
        32,
        data.size(),
        data.constData());
    xcb_flush(d->conn);
}

DWM_END_NAMESPACE
