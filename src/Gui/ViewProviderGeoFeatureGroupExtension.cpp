/***************************************************************************
 *   Copyright (c) 2011 Jürgen Riegel <FreeCAD@juergen-riegel.net>         *
 *   Copyright (c) 2015 Alexander Golubev (Fat-Zer) <fatzer2@gmail.com>    *
 *   Copyright (c) 2016 Stefan Tröger <stefantroeger@gmx.net>              *
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


#include "PreCompiled.h"

#ifndef _PreComp_
#endif

#include "ViewProviderGeoFeatureGroupExtension.h"
#include "ViewProviderLink.h"
#include "ViewParams.h"
#include "View3DInventor.h"
#include "Command.h"
#include "Application.h"
#include "Document.h"
#include <App/Application.h>
#include <App/DocumentObserver.h>
#include <App/GeoFeatureGroupExtension.h>
#include "SoFCUnifiedSelection.h"

using namespace Gui;

struct ViewProviderGeoFeatureGroupExtension::Private {
    std::vector<boost::signals2::scoped_connection> conns;
};

EXTENSION_PROPERTY_SOURCE(Gui::ViewProviderGeoFeatureGroupExtension, Gui::ViewProviderGroupExtension)

ViewProviderGeoFeatureGroupExtension::ViewProviderGeoFeatureGroupExtension()
    :impl(new Private), linkView(0)
{
    initExtensionType(ViewProviderGeoFeatureGroupExtension::getExtensionClassTypeId());

    if(ViewParams::instance()->getLinkChildrenDirect()) {
        linkView = new LinkView;
        pcGroupChildren = linkView->getLinkRoot();
    } else 
        pcGroupChildren = new SoFCSelectionRoot;

    pcGroupChildren->ref();
}

ViewProviderGeoFeatureGroupExtension::~ViewProviderGeoFeatureGroupExtension()
{
    if(linkView)
        linkView->setInvalid();

    impl.reset();

    pcGroupChildren->unref();
    pcGroupChildren = 0;
}

void ViewProviderGeoFeatureGroupExtension::extensionClaimChildren3D(
        std::vector<App::DocumentObject*> &children) const 
{

    //all object in the group must be claimed in 3D, as we are a coordinate system for all of them
    auto* ext = getExtendedViewProvider()->getObject()->getExtensionByType<App::GeoFeatureGroupExtension>();
    if (ext) {
        const auto &objs = ext->Group.getValues();
        children.insert(children.end(),objs.begin(),objs.end());
    }
}

void ViewProviderGeoFeatureGroupExtension::extensionClaimChildren(
        std::vector<App::DocumentObject *> &children) const 
{
    auto* group = getExtendedViewProvider()->getObject()->getExtensionByType<App::GeoFeatureGroupExtension>();
    buildExport();
    auto objs = group->_ExportChildren.getValues();
    children.insert(children.end(), objs.begin(), objs.end());
}

void ViewProviderGeoFeatureGroupExtension::extensionAttach(App::DocumentObject* pcObject)
{
    ViewProviderGroupExtension::extensionAttach(pcObject);
    getExtendedViewProvider()->addDisplayMaskMode(pcGroupChildren, "Group");
}

bool ViewProviderGeoFeatureGroupExtension::extensionHandleChildren3D(
        const std::vector<App::DocumentObject*> &) 
{
    if(linkView) {
        linkView->setChildren(getExtendedViewProvider()->claimChildren3D());
        return true;
    }
    return false;
} 

bool ViewProviderGeoFeatureGroupExtension::extensionGetElementPicked(
        const SoPickedPoint *pp, std::string &element) const 
{
    if(linkView) 
        return linkView->linkGetElementPicked(pp,element);
    return false;
}

bool ViewProviderGeoFeatureGroupExtension::extensionGetDetailPath(
        const char *subname, SoFullPath *path, SoDetail *&det) const 
{
    if(linkView)
        return linkView->linkGetDetailPath(subname,path,det);
    return false;
}

void ViewProviderGeoFeatureGroupExtension::extensionSetDisplayMode(const char* ModeName)
{
    if ( strcmp("Group",ModeName)==0 )
        getExtendedViewProvider()->setDisplayMaskMode("Group");

    ViewProviderGroupExtension::extensionSetDisplayMode( ModeName );
}

void ViewProviderGeoFeatureGroupExtension::extensionGetDisplayModes(std::vector<std::string> &StrList) const
{
    // get the modes of the father
    ViewProviderGroupExtension::extensionGetDisplayModes(StrList);

    // add your own modes
    StrList.push_back("Group");
}

void ViewProviderGeoFeatureGroupExtension::extensionUpdateData(const App::Property* prop)
{
    auto group = getExtendedViewProvider()->getObject()->getExtensionByType<App::GeoFeatureGroupExtension>();
    if(group) {
        if (prop == &group->Group) {

            buildExport();

            impl->conns.clear();
            if(linkView) {
                for(auto obj : group->Group.getValues()) {
                    // check for plain group
                    if(!obj || !obj->getNameInDocument())
                        continue;
                    auto ext = App::GeoFeatureGroupExtension::getNonGeoGroup(obj);
                    if(!ext)
                        continue;
                    impl->conns.push_back(
                            ext->Group.signalChanged.connect([=](const App::Property &){
                                auto owner = this->getExtendedViewProvider();
                                if(owner && this->linkView)
                                    this->linkView->setChildren(owner->claimChildren3D());
                            }));
                }
            }
        } else if(prop == &group->placement()) 
            getExtendedViewProvider()->setTransformation ( group->placement().getValue().toMatrix() );
    }
    ViewProviderGroupExtension::extensionUpdateData ( prop );
}

static void filterLinksByScope(const App::DocumentObject *obj, std::vector<App::DocumentObject *> &children)
{
    if(!obj || !obj->getNameInDocument())
        return;

    std::vector<App::Property*> list;
    obj->getPropertyList(list);
    std::set<App::DocumentObject *> links;
    for(auto prop : list) {
        auto link = Base::freecad_dynamic_cast<App::PropertyLinkBase>(prop);
        if(link && link->getScope()!=App::LinkScope::Global)
            link->getLinkedObjects(std::inserter(links,links.end()),true);
    }
    for(auto it=children.begin();it!=children.end();) {
        if(!links.count(*it))
            it = children.erase(it);
        else
            ++it;
    }
}

void ViewProviderGeoFeatureGroupExtension::buildExport() const {
    auto* group = getExtendedViewProvider()->getObject()->getExtensionByType<App::GeoFeatureGroupExtension>();
    if(!group)
        return;

    auto model = group->Group.getValues ();
    std::set<App::DocumentObject*> outSet; //< set of objects not to claim (childrens of childrens)

    // search for objects handled (claimed) by the features
    for (auto obj: model) {
        //stuff in another geofeaturegroup is not in the model anyway
        if (!obj || obj->hasExtension(App::GeoFeatureGroupExtension::getExtensionClassTypeId())) { continue; }

        Gui::ViewProvider* vp = Gui::Application::Instance->getViewProvider ( obj );
        if (!vp || vp == getExtendedViewProvider()) { continue; }

        auto children = vp->claimChildren();
        filterLinksByScope(obj,children);
        outSet.insert(children.begin(),children.end());
    }

    // remove the otherwise handled objects, preserving their order so the order in the TreeWidget is correct
    for(auto it=model.begin();it!=model.end();) {
        auto obj = *it;
        if(!obj || !obj->getNameInDocument() || outSet.count(obj))
            it = model.erase(it);
        else
            ++it;
    }

    if(group->_ExportChildren.getSize()!=(int)model.size())
        group->_ExportChildren.setValues(std::move(model));
}

int ViewProviderGeoFeatureGroupExtension::extensionReplaceObject(
        App::DocumentObject* oldValue, App::DocumentObject* newValue)
{
    auto owner = getExtendedViewProvider()->getObject();
    auto group = owner->getExtensionByType<App::GeoFeatureGroupExtension>();
    if(!group)
        return 0;

    if(group->_ExportChildren.find(oldValue->getNameInDocument()) != oldValue)
        return 0;

    auto children = group->_ExportChildren.getValues();
    for(auto &child : children) {
        if(child == oldValue)
            child = newValue;
    }

    std::vector<std::pair<App::DocumentObjectT, std::unique_ptr<App::Property> > > propChanges;

    // Global search for affected links
    for(auto doc : App::GetApplication().getDocuments()) {
        for(auto o : doc->getObjects()) {
            if(o == owner)
                continue;
            std::vector<App::Property*> props;
            o->getPropertyList(props);
            for(auto prop : props) {
                auto linkProp = Base::freecad_dynamic_cast<App::PropertyLinkBase>(prop);
                if(!linkProp)
                    continue;
                std::unique_ptr<App::Property> copy(
                        linkProp->CopyOnLinkReplace(owner,oldValue,newValue));
                if(!copy)
                    continue;
                propChanges.emplace_back(App::DocumentObjectT(prop),std::move(copy));
            }
        }
    }

    group->Group.setValues({});
    group->addObjects(children);

    for(auto &v : propChanges) {
        auto prop = v.first.getProperty();
        if(prop)
            prop->Paste(*v.second.get());
    }
    return 1;
}

namespace Gui {
EXTENSION_PROPERTY_SOURCE_TEMPLATE(Gui::ViewProviderGeoFeatureGroupExtensionPython, Gui::ViewProviderGeoFeatureGroupExtension)

// explicit template instantiation
template class GuiExport ViewProviderExtensionPythonT<ViewProviderGeoFeatureGroupExtension>;
}
