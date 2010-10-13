#include "res_collection.h"

#include <dlib/dstrings.h>
#include <dlib/log.h>

#include "gameobject.h"
#include "gameobject_private.h"

#include "../proto/gameobject_ddf.h"

namespace dmGameObject
{
    dmResource::CreateResult ResCollectionCreate(dmResource::HFactory factory,
                                                void* context,
                                                const void* buffer, uint32_t buffer_size,
                                                dmResource::SResourceDescriptor* resource,
                                                const char* filename)
    {
        Register* regist = (Register*) context;
        char prev_identifier_path[DM_GAMEOBJECT_CURRENT_IDENTIFIER_PATH_MAX];
        char tmp_ident[DM_GAMEOBJECT_CURRENT_IDENTIFIER_PATH_MAX];

        dmGameObjectDDF::CollectionDesc* collection_desc;
        dmDDF::Result e = dmDDF::LoadMessage<dmGameObjectDDF::CollectionDesc>(buffer, buffer_size, &collection_desc);
        if ( e != dmDDF::RESULT_OK )
        {
            return dmResource::CREATE_RESULT_UNKNOWN;
        }
        dmResource::CreateResult res = dmResource::CREATE_RESULT_OK;

        // NOTE: Be careful about control flow. See below with dmMutex::Unlock, return, etc
        dmMutex::Lock(regist->m_Mutex);

        HCollection collection;
        bool loading_root;
        if (regist->m_CurrentCollection == 0)
        {
            loading_root = true;
            // TODO: How to configure 1024. In collection?
            collection = NewCollection(factory, regist, 1024);
            collection->m_NameHash = dmHashString32(collection_desc->m_Name);
            regist->m_CurrentCollection = collection;
            regist->m_AccumulatedTranslation = Vector3(0, 0, 0);
            regist->m_AccumulatedRotation = Quat::identity();

            // NOTE: Root-collection name is not prepended to identifier
            prev_identifier_path[0] = '\0';
            regist->m_CurrentIdentifierPath[0] = '\0';
        }
        else
        {
            loading_root = false;
            collection = regist->m_CurrentCollection;
            dmStrlCpy(prev_identifier_path, regist->m_CurrentIdentifierPath, DM_GAMEOBJECT_CURRENT_IDENTIFIER_PATH_MAX);
        }

        for (uint32_t i = 0; i < collection_desc->m_Instances.m_Count; ++i)
        {
            const dmGameObjectDDF::InstanceDesc& instance_desc = collection_desc->m_Instances[i];
            dmGameObject::HInstance instance = dmGameObject::New(collection, instance_desc.m_Prototype);
            if (instance != 0x0)
            {
                Quat rot = regist->m_AccumulatedRotation * instance_desc.m_Rotation;
                Vector3 pos = rotate(regist->m_AccumulatedRotation, Vector3(instance_desc.m_Position)) + regist->m_AccumulatedTranslation;

                dmGameObject::SetPosition(instance, Point3(pos));
                dmGameObject::SetRotation(instance, rot);

                dmHashInit32(&instance->m_CollectionPathHashState);
                dmHashUpdateBuffer32(&instance->m_CollectionPathHashState, regist->m_CurrentIdentifierPath, strlen(regist->m_CurrentIdentifierPath));

                dmStrlCpy(tmp_ident, regist->m_CurrentIdentifierPath, sizeof(tmp_ident));
                dmStrlCat(tmp_ident, instance_desc.m_Id, sizeof(tmp_ident));

                if (dmGameObject::SetIdentifier(collection, instance, tmp_ident) != dmGameObject::RESULT_OK)
                {
                    dmLogError("Unable to set identifier %s for %s. Name clash?", tmp_ident, instance_desc.m_Id);
                }

                for (uint32_t j = 0; j < instance_desc.m_ScriptProperties.m_Count; ++j)
                {
                    const dmGameObjectDDF::Property& p = instance_desc.m_ScriptProperties[j];
                    switch (p.m_Type)
                    {
                        case dmGameObjectDDF::Property::STRING:
                            dmGameObject::SetScriptStringProperty(instance, p.m_Key, p.m_Value);
                        break;

                        case dmGameObjectDDF::Property::INTEGER:
                            dmGameObject::SetScriptIntProperty(instance, p.m_Key, atoi(p.m_Value));
                        break;

                        case dmGameObjectDDF::Property::FLOAT:
                            dmGameObject::SetScriptFloatProperty(instance, p.m_Key, atof(p.m_Value));
                        break;
                    }
                }
            }
            else
            {
                dmLogError("Could not instantiate game object from prototype %s.", instance_desc.m_Prototype);
                res = dmResource::CREATE_RESULT_UNKNOWN;
                goto bail;
            }
        }

        // Setup hierarchy
        for (uint32_t i = 0; i < collection_desc->m_Instances.m_Count; ++i)
        {
            const dmGameObjectDDF::InstanceDesc& instance_desc = collection_desc->m_Instances[i];

            dmStrlCpy(tmp_ident, regist->m_CurrentIdentifierPath, sizeof(tmp_ident));
            dmStrlCat(tmp_ident, instance_desc.m_Id, sizeof(tmp_ident));

            dmGameObject::HInstance parent = dmGameObject::GetInstanceFromIdentifier(collection, dmHashString32(tmp_ident));
            assert(parent);

            for (uint32_t j = 0; j < instance_desc.m_Children.m_Count; ++j)
            {
                dmGameObject::HInstance child = dmGameObject::GetInstanceFromIdentifier(collection, dmGameObject::GetAbsoluteIdentifier(parent, instance_desc.m_Children[j]));
                if (child)
                {
                    dmGameObject::Result r = dmGameObject::SetParent(child, parent);
                    if (r != dmGameObject::RESULT_OK)
                    {
                        dmLogError("Unable to set %s as parent to %s (%d)", instance_desc.m_Id, instance_desc.m_Children[j], r);
                    }
                    else
                    {
                        dmGameObject::SetScriptStringProperty(child, "Parent", tmp_ident);
                    }
                }
                else
                {
                    dmLogError("Child not found: %s", instance_desc.m_Children[j]);
                }
            }
        }

        // Load sub collections
        for (uint32_t i = 0; i < collection_desc->m_CollectionInstances.m_Count; ++i)
        {
            dmGameObjectDDF::CollectionInstanceDesc& coll_instance_desc = collection_desc->m_CollectionInstances[i];

            dmStrlCpy(prev_identifier_path, regist->m_CurrentIdentifierPath, DM_GAMEOBJECT_CURRENT_IDENTIFIER_PATH_MAX);
            dmStrlCat(regist->m_CurrentIdentifierPath, coll_instance_desc.m_Id, DM_GAMEOBJECT_CURRENT_IDENTIFIER_PATH_MAX);
            dmStrlCat(regist->m_CurrentIdentifierPath, ".", DM_GAMEOBJECT_CURRENT_IDENTIFIER_PATH_MAX);

            Collection* child_coll;
            Vector3 prev_translation = regist->m_AccumulatedTranslation;
            Quat prev_rotation = regist->m_AccumulatedRotation;

            Quat rot = regist->m_AccumulatedRotation * coll_instance_desc.m_Rotation;
            Vector3 trans = (regist->m_AccumulatedRotation * Quat(Vector3(coll_instance_desc.m_Position), 0.0f) * conj(regist->m_AccumulatedRotation)).getXYZ()
                       + regist->m_AccumulatedTranslation;

            regist->m_AccumulatedRotation = rot;
            regist->m_AccumulatedTranslation = trans;

            dmResource::FactoryResult r = dmResource::Get(factory, coll_instance_desc.m_Collection, (void**) &child_coll);
            if (r != dmResource::FACTORY_RESULT_OK)
            {
                res = dmResource::CREATE_RESULT_UNKNOWN;
                goto bail;
            }
            else
            {
                assert(child_coll != collection);
                dmResource::Release(factory, (void*) child_coll);
            }

            dmStrlCpy(regist->m_CurrentIdentifierPath, prev_identifier_path, DM_GAMEOBJECT_CURRENT_IDENTIFIER_PATH_MAX);

            regist->m_AccumulatedTranslation = prev_translation;
            regist->m_AccumulatedRotation = prev_rotation;
        }

        if (loading_root)
        {
            resource->m_Resource = (void*) collection;
        }
        else
        {
            // We must create a child collection. We can't return "collection" and release.
            // The root collection is not yet created.
            resource->m_Resource = (void*) NewCollection(factory, regist, 1);
        }
bail:
        dmDDF::FreeMessage(collection_desc);
        dmStrlCpy(regist->m_CurrentIdentifierPath, prev_identifier_path, DM_GAMEOBJECT_CURRENT_IDENTIFIER_PATH_MAX);

        if (loading_root && res != dmResource::CREATE_RESULT_OK)
        {
            // Loading of root-collection is responsible for deleting
            DeleteCollection(collection);
        }

        if (loading_root)
        {
            // We must reset this to next load.
            regist->m_CurrentCollection = 0;
        }

        dmMutex::Unlock(regist->m_Mutex);
        return res;
    }

    dmResource::CreateResult ResCollectionDestroy(dmResource::HFactory factory,
                                                 void* context,
                                                 dmResource::SResourceDescriptor* resource)
    {
        HCollection collection = (HCollection) resource->m_Resource;
        DeleteCollection(collection);
        return dmResource::CREATE_RESULT_OK;
    }
}
