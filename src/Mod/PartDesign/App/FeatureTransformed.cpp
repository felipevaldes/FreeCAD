/******************************************************************************
 *   Copyright (c) 2012 Jan Rheinländer <jrheinlaender@users.sourceforge.net> *
 *                                                                            *
 *   This file is part of the FreeCAD CAx development system.                 *
 *                                                                            *
 *   This library is free software; you can redistribute it and/or            *
 *   modify it under the terms of the GNU Library General Public              *
 *   License as published by the Free Software Foundation; either             *
 *   version 2 of the License, or (at your option) any later version.         *
 *                                                                            *
 *   This library  is distributed in the hope that it will be useful,         *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *   GNU Library General Public License for more details.                     *
 *                                                                            *
 *   You should have received a copy of the GNU Library General Public        *
 *   License along with this library; see the file COPYING.LIB. If not,       *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,            *
 *   Suite 330, Boston, MA  02111-1307, USA                                   *
 *                                                                            *
 ******************************************************************************/


#include "PreCompiled.h"
#ifndef _PreComp_
# include <BRepBuilderAPI_Transform.hxx>
# include <BRepAlgoAPI_Fuse.hxx>
# include <BRepAlgoAPI_Cut.hxx>
# include <BRep_Builder.hxx>
# include <TopExp.hxx>
# include <TopExp_Explorer.hxx>
# include <TopTools_IndexedMapOfShape.hxx>
# include <Precision.hxx>
# include <BRepBuilderAPI_Copy.hxx>
# include <BRepBndLib.hxx>
# include <Bnd_Box.hxx>
#endif


#include "FeatureTransformed.h"
#include "FeatureMultiTransform.h"
#include "FeatureAddSub.h"
#include "FeatureMirrored.h"
#include "FeatureLinearPattern.h"
#include "FeaturePolarPattern.h"
#include "FeatureSketchBased.h"
#include "Body.h"

#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Parameter.h>
#include <Base/Reader.h>
#include <App/Application.h>
#include <App/Document.h>
#include <App/MappedElement.h>
#include <Mod/Part/App/modelRefine.h>

FC_LOG_LEVEL_INIT("PartDesign",true,true)

using namespace PartDesign;
using namespace Part;

namespace PartDesign {

PROPERTY_SOURCE(PartDesign::Transformed, PartDesign::FeatureAddSub)

Transformed::Transformed()
{
    ADD_PROPERTY(Originals,(0));
    Originals.setSize(0);
    Originals.setStatus(App::Property::Hidden, true);

    ADD_PROPERTY(OriginalSubs,(0));
    Placement.setStatus(App::Property::ReadOnly, true);

    ADD_PROPERTY_TYPE(Refine,(0),"Part Design",(App::PropertyType)(App::Prop_None),"Refine shape (clean up redundant edges) after adding/subtracting");

    ADD_PROPERTY_TYPE(SubTransform,(true),"Part Design",(App::PropertyType)(App::Prop_None),
        "Transform sub feature instead of the solid if it is an additive or substractive feature (e.g. Pad, Pocket)");
    ADD_PROPERTY_TYPE(CopyShape,(true),"Part Design",(App::PropertyType)(App::Prop_None),
        "Make a copy of each transformed shape");

    ADD_PROPERTY_TYPE(ParallelTransform,(true),"Part Design",(App::PropertyType)(App::Prop_None),
        "Perform boolean operation on transformed feature in parallel");

    ADD_PROPERTY_TYPE(TransformOffset,(Base::Placement()),"Part Design",(App::PropertyType)(App::Prop_None),
        "Offset placement applied to the source shape before pattern transformation.");

    ADD_PROPERTY_TYPE(_Version,(0),"Part Design",(App::PropertyType)(App::Prop_Hidden), 0);

    //init Refine property
    Base::Reference<ParameterGrp> hGrp = App::GetApplication().GetUserParameter()
        .GetGroup("BaseApp")->GetGroup("Preferences")->GetGroup("Mod/PartDesign");
    this->Refine.setValue(hGrp->GetBool("RefineModel", false));
}

void Transformed::positionBySupport(void)
{
    // TODO May be here better to throw exception (silent=false) (2015-07-27, Fat-Zer)
    Part::Feature *support = getBaseObject(/* silent =*/ true);
    if (support)
        this->Placement.setValue(support->Placement.getValue());
}

Part::Feature* Transformed::getBaseObject(bool silent) const {
    Part::Feature *rv = Feature::getBaseObject(/* silent = */ true);
    if (rv) {
        return rv;
    }

    const char* err = nullptr;
    const std::vector<App::DocumentObject*> & originals = OriginalSubs.getValues();
    // NOTE: may be here supposed to be last origin but in order to keep the old behaviour keep here first 
    App::DocumentObject* firstOriginal = originals.empty() ? NULL : originals.front();
    if (firstOriginal) {
        if(firstOriginal->isDerivedFrom(Part::Feature::getClassTypeId())) {
            rv = static_cast<Part::Feature*>(firstOriginal);
        } else {
            err = "Transformation feature Linked object is not a Part object";
        }
    } else {
        err = "No originals linked to the transformed feature.";
    }

    if (!silent && err) {
        throw Base::RuntimeError(err);
    }

    return rv;
}

App::DocumentObject* Transformed::getSketchObject() const
{
    std::vector<DocumentObject*> originals = OriginalSubs.getValues();
    if (!originals.empty() && originals.front()->getTypeId().isDerivedFrom(PartDesign::ProfileBased::getClassTypeId())) {
        return (static_cast<PartDesign::ProfileBased*>(originals.front()))->getVerifiedSketch(true);
    }
    else if (!originals.empty() && originals.front()->getTypeId().isDerivedFrom(PartDesign::FeatureAddSub::getClassTypeId())) {
        return NULL;
    }
    else if (this->getTypeId().isDerivedFrom(LinearPattern::getClassTypeId())) {
        // if Originals is empty then try the linear pattern's Direction property
        const LinearPattern* pattern = static_cast<const LinearPattern*>(this);
        return pattern->Direction.getValue();
    }
    else if (this->getTypeId().isDerivedFrom(PolarPattern::getClassTypeId())) {
        // if Originals is empty then try the polar pattern's Axis property
        const PolarPattern* pattern = static_cast<const PolarPattern*>(this);
        return pattern->Axis.getValue();
    }
    else if (this->getTypeId().isDerivedFrom(Mirrored::getClassTypeId())) {
        // if Originals is empty then try the mirror pattern's MirrorPlane property
        const Mirrored* pattern = static_cast<const Mirrored*>(this);
        return pattern->MirrorPlane.getValue();
    }
    else {
        return 0;
    }
}

void Transformed::handleChangedPropertyType(
        Base::XMLReader &reader, const char * TypeName, App::Property * prop) 
{
    Base::Type inputType = Base::Type::fromName(TypeName);
    // The property 'Angle' of PolarPattern has changed from PropertyFloat
    // to PropertyAngle and the property 'Length' has changed to PropertyLength.
    if (prop->getTypeId().isDerivedFrom(App::PropertyFloat::getClassTypeId()) &&
            inputType.isDerivedFrom(App::PropertyFloat::getClassTypeId())) {
        // Do not directly call the property's Restore method in case the implementation
        // has changed. So, create a temporary PropertyFloat object and assign the value.
        App::PropertyFloat floatProp;
        floatProp.Restore(reader);
        static_cast<App::PropertyFloat*>(prop)->setValue(floatProp.getValue());
    }
}

short Transformed::mustExecute() const
{
    if (OriginalSubs.isTouched())
        return 1;
    return PartDesign::Feature::mustExecute();
}

App::DocumentObjectExecReturn *Transformed::execute(void)
{
    rejected.clear();

    auto body = getFeatureBody();

    auto originals = OriginalSubs.getSubListValues(true);
    for (auto it=originals.begin(); it!=originals.end(); ) {
        auto &v = *it;
        auto obj = Base::freecad_dynamic_cast<PartDesign::Feature>(v.first);
        if(!obj) 
            it = originals.erase(it);
        else
            ++it;
    }
    std::vector<std::pair<int, int> > originalIndices;
    if (originals.empty()) {
        if(!BaseFeature.getValue()) {
            // typically InsideMultiTransform
            Shape.setValue(TopoShape());
            AddSubShape.setValue(TopoShape());
            return App::DocumentObject::StdReturn;
        }
        std::vector<std::string> subs;
        subs.emplace_back("");
        originals.emplace_back(BaseFeature.getValue(),subs);
    } else if (body && originals.size() > 1) {
        originalIndices.reserve(originals.size());
        int i=0;
        for (auto &v : originals) {
            int idx;
            if (!body->Group.find(v.first->getNameInDocument(), &idx))
                continue;
            originalIndices.emplace_back(idx, i++);
        }

        // sort the original objects in history order
        std::sort(originalIndices.begin(), originalIndices.end(),
            [&](const std::pair<int,int> &a,
                const std::pair<int,int> &b)
            {
                return a.first < b.first;
            });
    }

    if(!this->BaseFeature.getValue()) {
        if(body)
            body->setBaseProperty(this);
    }

    this->positionBySupport();
    bool hasOffset = !TransformOffset.getValue().isIdentity();

    // Get the support
    TopoShape support;
    bool canSkipFirst = true;
    auto baseObj = getBaseObject(true);
    if (NewSolid.getValue() || !baseObj) 
        canSkipFirst = false;
    else {
        support = getBaseShape(true);
        if (support.isNull())
            return new App::DocumentObjectExecReturn("Cannot transform invalid support shape");

        if (_Version.getValue() > 1
                && hasOffset
                && SubTransform.getValue()
                && !Base::freecad_dynamic_cast<Transformed>(baseObj)
                && Base::freecad_dynamic_cast<FeatureAddSub>(baseObj)) {
            for (auto &v : originals) {
                if (baseObj != v.first)
                    continue;
                PartDesign::FeatureAddSub* feature = static_cast<PartDesign::FeatureAddSub*>(v.first);
                if(!feature->Suppress.getValue()) {
                    support = feature->getBaseShape(true);
                    if (baseObj)
                        this->Placement.setValue(baseObj->Placement.getValue());
                    canSkipFirst = false;
                }
                break;
            }
        } 
    }

    auto trsfInv = support.getShape().Location().Transformation().Inverted();
    if (hasOffset)
        trsfInv.Multiply(TopoShape::convert(TransformOffset.getValue().toMatrix()));

    // create an untransformed copy of the support shape
    support.setTransform(Base::Matrix4D());
    if(!support.Hasher)
        support.Hasher = getDocument()->getStringHasher();

    std::unordered_set<TopoDS_Shape, Part::ShapeHasher, Part::ShapeHasher> addshapeSet;
    std::unordered_set<TopoDS_Shape, Part::ShapeHasher, Part::ShapeHasher> cutshapeSet;
    std::vector<TopoShape> originalShapes;
    std::vector<std::string> originalSubs;
    std::vector<bool> fuses;
    std::vector<int> startIndices;
    for (std::size_t i=0; i<originals.size(); ++i) {
        auto &v = originalIndices.size()>i ? originals[originalIndices[i].second] : originals[i];
        auto obj = Base::freecad_dynamic_cast<PartDesign::Feature>(v.first);
        if(!obj) 
            continue;

        int startIndex = canSkipFirst && body && body->isSibling(this, obj) ? 1 : 0;

        if (SubTransform.getValue() 
                && obj->isDerivedFrom(PartDesign::FeatureAddSub::getClassTypeId())) 
        {
            if(obj->Suppress.getValue())
                continue;
            PartDesign::FeatureAddSub* feature = static_cast<PartDesign::FeatureAddSub*>(obj);
            std::vector<std::pair<Part::TopoShape, bool> > addsubshapes;
            feature->getAddSubShape(addsubshapes);
            if (addsubshapes.empty())
                continue;
            for (auto &v : addsubshapes) {
                auto &shape = v.first;
                if(shape.isNull())
                    continue;
                auto &shapeSet = v.second ? addshapeSet : cutshapeSet;
                if (!shapeSet.insert(shape.getShape()).second)
                    continue;
                shape.Tag = -shape.Tag;
                auto trsf = feature->getLocation().Transformation().Multiplied(trsfInv);
                originalShapes.push_back(shape.makETransform(trsf));
                originalSubs.push_back(feature->getFullName());
                fuses.push_back(v.second);
                startIndices.push_back(startIndex);
            }
            continue;
        } 

        for(auto &sub : v.second) {
            TopoShape baseShape = obj->Shape.getShape();
            std::vector<TopoShape> shapes;
            if(sub.empty()) 
                shapes = baseShape.getSubTopoShapes(TopAbs_SOLID);
            else {
                TopoDS_Shape subShape = baseShape.getSubShape(sub.c_str());
                if(subShape.IsNull())
                    return new App::DocumentObjectExecReturn("Shape of source feature is empty");
                int idx = baseShape.findAncestor(subShape,TopAbs_SOLID);
                if(idx)
                    shapes.push_back(baseShape.getSubTopoShape(TopAbs_SOLID,idx));
            }
            if(shapes.empty())
                return new App::DocumentObjectExecReturn("Non solid source feature");
            for(auto &s : shapes) {
                if (!addshapeSet.insert(s.getShape()).second)
                    continue;
                originalShapes.push_back(s.makETransform(trsfInv));
                if(sub.size())
                    originalSubs.push_back(obj->getFullName() + '.' + sub);
                else
                    originalSubs.push_back(obj->getFullName());
                fuses.push_back(true);
                startIndices.push_back(startIndex);
            }
        }
    }

    // get transformations from subclass by calling virtual method
    std::vector<gp_Trsf> transformations;
    try {
        std::list<gp_Trsf> t_list = getTransformations(originalShapes);
        transformations.insert(transformations.end(), t_list.begin(), t_list.end());
    } catch (Base::Exception& e) {
        return new App::DocumentObjectExecReturn(e.what());
    }

    if (transformations.empty() || originalShapes.empty()) {
        Shape.setValue(support);
        return App::DocumentObject::StdReturn; // No transformations defined, exit silently
    }

    std::ostringstream ss;

    TopoShape result;

    FC_TIME_INIT(t);

    std::vector<std::pair<TopoShape, bool> > addsub;

    if (allowMultiSolid() && ParallelTransform.getValue()) {
        std::vector<TopoShape> fuseShapes;
        bool hasSupport = false;
        if (!support.isNull()) {
            hasSupport = true;
            fuseShapes.push_back(support);
        }
        std::vector<TopoShape> cutShapes;
        cutShapes.push_back(TopoShape());

        auto buildShape = [&]() {
            try {
                try {
                    if (fuseShapes.size() > 1) {
                        if (cutShapes.size() <= 1 && NewSolid.getValue()) {
                            support.makECompound(fuseShapes);
                            addsub.emplace_back(support, true);
                        } else {
                            if (!isRecomputePaused())
                                support.makEFuse(fuseShapes);
                            if (hasSupport)
                                fuseShapes.erase(fuseShapes.begin());
                            addsub.emplace_back(
                                    TopoShape().makECompound(fuseShapes, nullptr, false), true);
                        }
                        fuseShapes.clear();
                        fuseShapes.push_back(support);
                        hasSupport = true;
                        result = support;
                    }
                    else if(cutShapes.size() > 1) {
                        if (support.isNull()) { // means new solid without fuseShapes
                            if (cutShapes.size() == 2)
                                result = cutShapes[1];
                            else {
                                cutShapes.erase(cutShapes.begin());
                                result.makECompound(cutShapes);
                            }
                            addsub.emplace_back(result, false);
                        } else {
                            cutShapes[0] = support;
                            if (!isRecomputePaused())
                                result.makECut(cutShapes);
                            support = result;
                            cutShapes.erase(cutShapes.begin());
                            addsub.emplace_back(TopoShape().makECompound(
                                        cutShapes, nullptr, false), false);
                        }
                        cutShapes.clear();
                        cutShapes.push_back(TopoShape());
                        if (fuseShapes.empty())
                            fuseShapes.push_back(support);
                        else
                            fuseShapes[0] = support;
                        hasSupport = true;
                    }
                    else if (support.isNull() && fuseShapes.size() == 1) {
                        // Must wrap the shape to contain its placement
                        support.makECompound(fuseShapes);
                        addsub.emplace_back(support, true);
                        result = support;
                        hasSupport = true;
                    }

                } catch (Standard_Failure& e) {
                    std::string msg("Boolean operation failed");
                    if (e.GetMessageString() != NULL)
                        msg += std::string(": '") + e.GetMessageString() + "'";
                    throw Base::CADKernelError(msg.c_str());
                }
            } catch (Base::Exception &) {
                for(auto &s : cutShapes)
                    rejected.emplace_back(s,std::vector<gp_Trsf>());
                for(auto &s : fuseShapes)
                    rejected.emplace_back(s,std::vector<gp_Trsf>());
                throw;
            }
        };

        int i=0;
        bool lastfuse = true;
        for (const TopoShape &shape : originalShapes) {
            auto &sub = originalSubs[i];
            int idx = startIndices[i];
            bool fuse = fuses[i++];
            if (fuse != lastfuse) {
                lastfuse = fuse;
                buildShape();
            }
            std::vector<gp_Trsf>::const_iterator t = transformations.begin();
            for (; t != transformations.end(); ++t,++idx) {
                ss.str("");
                if (idx)
                    ss << 'I' << idx;
                auto shapeCopy = CopyShape.getValue()?shape.makECopy():shape;
                if (shapeCopy.isNull())
                    return new App::DocumentObjectExecReturn("Transformed: Linked shape object is empty");
                try {
                    shapeCopy = shapeCopy.makETransform(*t, ss.str().c_str());
                    if (idx == 0 && canSkipFirst && (_Version.getValue()==0 || !hasOffset)) {
                        // Skip first transformation in case we do not transform the
                        // first instance (i.e. original feature belongs to the same
                        // sibling group)
                        addsub.emplace_back(shapeCopy, fuse);
                        continue; 
                    }
                    if(fuse)
                        fuseShapes.push_back(shapeCopy);
                    else
                        cutShapes.push_back(shapeCopy);
                }catch(Standard_Failure &) {
                    rejected.emplace_back(shape,std::vector<gp_Trsf>(t,t+1));
                    std::string msg("Transformation failed ");
                    msg += sub;
                    return new App::DocumentObjectExecReturn(msg.c_str());
                }
            }
        }

        buildShape();
        originalShapes.clear();
    }


    // NOTE: It would be possible to build a compound from all original addShapes/subShapes and then
    // transform the compounds as a whole. But we choose to apply the transformations to each
    // Original separately. This way it is easier to discover what feature causes a fuse/cut
    // to fail. The downside is that performance suffers when there are many originals. But it seems
    // safe to assume that in most cases there are few originals and many transformations
    int i=0;
    std::vector<TopoShape> fuseShapes;
    std::vector<TopoShape> cutShapes;
    for (TopoShape &shape : originalShapes) {
        auto &sub = originalSubs[i];
        int idx = startIndices[i];
        bool fuse = fuses[i++];

        // Transform the add/subshape and collect the resulting shapes for overlap testing
        /*typedef std::vector<std::vector<gp_Trsf>::const_iterator> trsf_it_vec;
        trsf_it_vec v_transformations;
        std::vector<TopoDS_Shape> v_transformedShapes;*/

        std::vector<gp_Trsf>::const_iterator t = transformations.begin();
        if (idx != 0)
            ++t; // Skip first transformation, which is always the identity transformation
        for (; t != transformations.end(); ++t,++idx) {
            // Make an explicit copy of the shape because the "true" parameter to BRepBuilderAPI_Transform
            // seems to be pretty broken
            ss.str("");
            ss << 'I' << idx;
            auto shapeCopy = CopyShape.getValue()?shape.makECopy():shape;
            if (shapeCopy.isNull())
                return new App::DocumentObjectExecReturn("Transformed: Linked shape object is empty");

            try {
                shapeCopy = shapeCopy.makETransform(*t, ss.str().c_str());
            }catch(Standard_Failure &) {
                std::string msg("Transformation failed ");
                msg += sub;
                return new App::DocumentObjectExecReturn(msg.c_str());
            }

            try {
                // Intersection checking for additive shape is redundant.
                // Because according to CheckIntersection() source code, it is
                // implemented using fusion and counting of the resulting
                // solid, which will be done in the following modeling step
                // anyway.
                //
                // There is little reason for doing intersection checking on
                // subtractive shape either, because it does not produce
                // multiple solids.
                //
                // if (!Part::checkIntersection(support, mkTrf.Shape(), false, true)) 


                addsub.emplace_back(shapeCopy, fuse);
                if (isRecomputePaused())
                    continue;
                if (fuse) {
                    fuseShapes.push_back(shapeCopy);
                    result = support.makEFuse(shapeCopy);
                } else {
                    cutShapes.push_back(shapeCopy);
                    result = support.makECut(shapeCopy);
                }
                // Do not call getSolid() as we need the compound to hide the
                // placement
                support = _Version.getValue()>1 ? result : getSolid(result);
                // lets check if the result is a solid
                if (support.isNull()) {
                    std::string msg("Resulting shape is not a solid: ");
                    msg += sub;
                    return new App::DocumentObjectExecReturn(msg.c_str());
                }

            } catch (Standard_Failure& e) {
                // Note: Ignoring this failure is probably pointless because if the intersection check fails, the later
                // fuse operation of the transformation result will also fail

                rejected.emplace_back(shape,std::vector<gp_Trsf>(t,t+1));
                std::string msg("Transformation: Intersection check failed");
                if (e.GetMessageString() != NULL)
                    msg += std::string(": '") + e.GetMessageString() + "'";
                return new App::DocumentObjectExecReturn(msg.c_str());
            }
        }
    }

    if (addsub.size() == 0)
        AddSubShape.setValue(TopoShape());
    else {
        std::vector<TopoShape> addsubshapes;
        std::vector<TopoShape> tmpshapes;
        // compact consecutive shapes that has the same operation (fuse or cut)
        for (std::size_t i=0; i<addsub.size(); ++i) {
            bool fuse = addsub[i].second;
            for (auto it=addsub.begin()+i+1; it!=addsub.end();) {
                if (it->second != fuse)
                    break;
                if (tmpshapes.empty())
                    tmpshapes.push_back(addsub[i].first);
                tmpshapes.push_back(it->first);
                it = addsub.erase(it);
            }
            if (tmpshapes.size()) {
                addsub[i].first.makECompound(tmpshapes);
                tmpshapes.clear();
            }
        }
        if (addsub.size() == 1 && addsub[0].first.shapeType() != TopAbs_COMPOUND) {
            if (addsub[0].second)
                addsubshapes.push_back(addsub[0].first);
            else {
                std::vector<TopoShape> tmpshapes;
                // Push an empty compound to mark the cut shape
                tmpshapes.push_back(TopoShape().makECompound({}));
                tmpshapes.push_back(addsub[0].first);
                addsubshapes.push_back(TopoShape().makECompound(tmpshapes));
            }
            AddSubShape.setValue(addsubshapes[0]);
        }
        else if (addsub.size() == 2
                && addsub[0].second
                && !addsub[1].second
                && addsub[0].first.shapeType() != TopAbs_COMPOUND
                && addsub[1].first.shapeType() != TopAbs_COMPOUND) {
            addsubshapes.push_back(addsub[0].first);
            addsubshapes.push_back(addsub[1].first);
            AddSubShape.setValue(TopoShape().makECompound(addsubshapes));
        }
        else {
            std::vector<TopoShape> tmpshapes;
            for (auto &v : addsub) {
                if (!v.second) {
                    // Add an empty compound shape to mark this is a subtractive shape
                    tmpshapes.push_back(TopoShape().makECompound({}));
                }
                tmpshapes.push_back(v.first);
                addsubshapes.push_back(TopoShape().makECompound(tmpshapes));
                tmpshapes.clear();
            }
            AddSubShape.setValue(TopoShape().makECompound(addsubshapes));
        }
    }

    if (!isRecomputePaused()) {
        result = refineShapeIfActive(result);

        FC_TIME_LOG(t,"done");

        // Do not call getSolid() as we need the compound to hide the placement
        this->Shape.setValue(_Version.getValue()>1 ? result : getSolid(result));
    }

    if (rejected.size() > 0) {
        return new App::DocumentObjectExecReturn("Transformation failed");
    }

    return App::DocumentObject::StdReturn;
}

TopoShape Transformed::refineShapeIfActive(const TopoShape& oldShape) const
{
    if (this->Refine.getValue()) 
        return oldShape.makERefine();
    return oldShape;
}

void Transformed::divideTools(const std::vector<TopoDS_Shape> &toolsIn, std::vector<TopoDS_Shape> &individualsOut,
                              TopoDS_Compound &compoundOut) const
{
    typedef std::pair<TopoDS_Shape, Bnd_Box> ShapeBoundPair;
    typedef std::list<ShapeBoundPair> PairList;
    typedef std::vector<ShapeBoundPair> PairVector;

    PairList pairList;

    std::vector<TopoDS_Shape>::const_iterator it;
    for (it = toolsIn.begin(); it != toolsIn.end(); ++it) {
        Bnd_Box bound;
        BRepBndLib::Add(*it, bound);
        bound.SetGap(0.0);
        ShapeBoundPair temp = std::make_pair(*it, bound);
        pairList.push_back(temp);
    }

    BRep_Builder builder;
    builder.MakeCompound(compoundOut);

    while(!pairList.empty()) {
        PairVector currentGroup;
        currentGroup.push_back(pairList.front());
        pairList.pop_front();
        PairList::iterator it = pairList.begin();
        while(it != pairList.end()) {
            PairVector::const_iterator groupIt;
            bool found(false);
            for (groupIt = currentGroup.begin(); groupIt != currentGroup.end(); ++groupIt) {
                if (!(*it).second.IsOut((*groupIt).second)) {//touching means is out.
                    found = true;
                    break;
                }
            }
            if (found) {
                currentGroup.push_back(*it);
                pairList.erase(it);
                it=pairList.begin();
                continue;
            }
            ++it;
        }

        if (currentGroup.size() == 1) {
            builder.Add(compoundOut, currentGroup.front().first);
        }
        else {
            PairVector::const_iterator groupIt;
            for (groupIt = currentGroup.begin(); groupIt != currentGroup.end(); ++groupIt)
                individualsOut.push_back((*groupIt).first);
        }
    }
}

void Transformed::onDocumentRestored() {
    if(OriginalSubs.getValues().empty() && Originals.getSize()) {
        std::vector<std::string> subs(Originals.getSize());
        OriginalSubs.setValues(Originals.getValues(),subs);
    }
    PartDesign::Feature::onDocumentRestored();
}

void Transformed::onChanged(const App::Property *prop) {
    if (!this->isRestoring() 
            && this->getDocument()
            && !this->getDocument()->isPerformingTransaction()
            && !prop->testStatus(App::Property::User3))
    {
        if(prop == &Originals) {
            std::map<App::DocumentObject*,std::vector<std::string> > subMap;
            const auto &originals = OriginalSubs.getSubListValues();
            for(auto &v : originals) {
                auto &subs = subMap[v.first];
                subs.insert(subs.end(),v.second.begin(),v.second.end());
            }
            std::vector<App::PropertyLinkSubList::SubSet> subset;
            std::set<App::DocumentObject*> objSet;
            bool touched = false;
            for(auto obj : Originals.getValues()) {
                if(!objSet.insert(obj).second)
                    continue;
                auto it = subMap.find(obj);
                if(it == subMap.end()) {
                    touched = true;
                    subset.emplace_back(obj,std::vector<std::string>{std::string("")});
                    continue;
                }
                subset.emplace_back(it->first,std::move(it->second));
                subMap.erase(it);
            }
            if(subMap.size() || touched || originals!=subset) {
                OriginalSubs.setStatus(App::Property::User3,true);
                OriginalSubs.setSubListValues(subset);
                OriginalSubs.setStatus(App::Property::User3,false);
            }
        }else if(prop == &OriginalSubs) {
            std::set<App::DocumentObject*> objSet;
            std::vector<App::DocumentObject *> objs;
            for(auto obj : OriginalSubs.getValues()) {
                if(objSet.insert(obj).second)
                    objs.push_back(obj);
            }
            if(objs != Originals.getValues()) {
                Originals.setStatus(App::Property::User3,true);
                Originals.setValues(objs);
                Originals.setStatus(App::Property::User3,false);
            }
        }
    }
    PartDesign::Feature::onChanged(prop);
}

void Transformed::setupObject () {
    CopyShape.setValue(false);
    _Version.setValue(2);
}

bool Transformed::isElementGenerated(const TopoShape &shape, const Data::MappedName &name) const
{
    bool res = false;
    long tag = 0;
    int depth = 2;
    shape.traceElement(name,
        [&] (const Data::MappedName &, int, long tag2, long) {
            if(tag && std::abs(tag2)!=tag) {
                if(--depth == 0)
                    return true;
            }
            if(tag2 < 0) {
                tag2 = -tag2;
                if (this->OriginalSubs.getValues().empty()) {
                    if(BaseFeature.getValue() && tag2 == BaseFeature.getValue()->getID()) {
                        res = true;
                        return true;
                    }
                }
                for(auto obj : this->OriginalSubs.getValues()) {
                    if(tag2 == obj->getID()) {
                        res = true;
                        return true;
                    }
                }
            }
            tag = tag2;
            return false;
        });

    return res;
}

void Transformed::getAddSubShape(std::vector<std::pair<Part::TopoShape, bool> > &shapes)
{
    Part::TopoShape res = AddSubShape.getShape();
    if (res.isNull())
        return;
    if (res.shapeType(true) != TopAbs_COMPOUND) {
        shapes.emplace_back(res, true);
        return;
    }
    int count = 0;
    auto subshapes = res.getSubTopoShapes();
    if (subshapes.size() > 0 && subshapes.size() <= 2) {
        auto s = subshapes[0];
        bool proceed = false;
        if (s.isNull())
            proceed = true;
        else if (s.shapeType(true)!=TopAbs_COMPOUND
                || s.countSubShapes(TopAbs_SHAPE) != 0) {
            ++count;
            shapes.emplace_back(s, true);
            proceed = true;
        }
        if (proceed && subshapes.size() > 1) {
            auto s = subshapes[1];
            if (!s.isNull()) {
                if (s.shapeType(true) != TopAbs_COMPOUND
                        || s.countSubShapes(TopAbs_SHAPE) != 0) {
                    ++count;
                    shapes.emplace_back(s, false);
                }
            }
        }
    }
    if (!count) {
        for (auto &subshape : subshapes) {
            if (subshape.isNull())
                continue;
            if (subshape.shapeType(true) != TopAbs_COMPOUND) {
                shapes.emplace_back(subshape, true);
                continue;
            }
            auto s = subshape.getSubTopoShape(TopAbs_SHAPE, 1, true);
            if (!s.isNull()) {
                if (s.shapeType(true) != TopAbs_COMPOUND
                        || s.countSubShapes(TopAbs_SHAPE) != 0)
                    shapes.emplace_back(s, true);
            }
            s = subshape.getSubTopoShape(TopAbs_SHAPE, 2, true);
            if (!s.isNull()) {
                if (s.shapeType(true) != TopAbs_COMPOUND
                        || s.countSubShapes(TopAbs_SHAPE) != 0)
                    shapes.emplace_back(s, false);
            }
        }
    }
}

}
