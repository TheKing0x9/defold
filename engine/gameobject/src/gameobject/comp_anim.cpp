
#include "comp_anim.h"

#include <dlib/profile.h>

#include <script/script.h>

#include "gameobject_script.h"
#include "gameobject_private.h"
#include "gameobject_props_lua.h"

extern "C"
{
#include <lua/lauxlib.h>
#include <lua/lualib.h>
}

namespace dmGameObject
{
#define INVALID_INDEX 0xffff
#define MAX_CAPACITY 65000u
#define MIN_CAPACITY_GROWTH 2048u

    struct Animation
    {
        HInstance           m_Instance;
        dmhash_t            m_ComponentId;
        dmhash_t            m_PropertyId;
        Playback            m_Playback;
        dmEasing::Type      m_Easing;
        float*              m_Value;
        float               m_From;
        float               m_To;
        float               m_Delay;
        float               m_Cursor;
        float               m_Duration;
        float               m_InvDuration;
        AnimationStopped    m_AnimationStopped;
        void*               m_Userdata1;
        void*               m_Userdata2;
        uint16_t            m_Index;
        uint16_t            m_Next;
        uint16_t            m_Playing : 1;
        uint16_t            m_Finished : 1;
        uint16_t            m_Composite : 1;
        uint16_t            m_Backwards : 1;
        uint16_t            m_FirstUpdate : 1;
    };

    struct AnimWorld
    {
        dmArray<Animation>                  m_Animations;
        dmArray<uint16_t>                   m_AnimMap;
        dmIndexPool<uint16_t>               m_AnimMapIndexPool;
        dmHashTable<uintptr_t, uint16_t>    m_InstanceToIndex;
        uint32_t                            m_InUpdate : 1;
    };

    CreateResult CompAnimNewWorld(const ComponentNewWorldParams& params)
    {
        if (params.m_World != 0x0)
        {
            AnimWorld* world = new AnimWorld();
            *params.m_World = world;
            const uint32_t anim_count = 512;
            world->m_Animations.SetCapacity(anim_count);
            world->m_AnimMap.SetCapacity(MAX_CAPACITY);
            world->m_AnimMap.SetSize(MAX_CAPACITY);
            world->m_AnimMapIndexPool.SetCapacity(MAX_CAPACITY);
            // This is fetched from res_collection.cpp (ResCollectionCreate)
            const uint32_t instance_count = 1024;
            world->m_InstanceToIndex.SetCapacity(instance_count/3, instance_count);
            world->m_InUpdate = 0;
            return CREATE_RESULT_OK;
        }
        else
        {
            return CREATE_RESULT_UNKNOWN_ERROR;
        }
    }

    CreateResult CompAnimDeleteWorld(const ComponentDeleteWorldParams& params)
    {
        if (params.m_World != 0x0)
        {
            delete (AnimWorld*)params.m_World;
            return CREATE_RESULT_OK;
        }
        else
        {
            return CREATE_RESULT_UNKNOWN_ERROR;
        }
    }

    static void StopAnimation(Animation* anim, bool finished)
    {
        anim->m_Finished = finished;
        anim->m_Playing = 0;
    }

    static void StopAnimations(AnimWorld* world, uint16_t* head_ptr, dmhash_t component_id, dmhash_t property_id)
    {
        if (head_ptr != 0x0)
        {
            uint16_t index = *head_ptr;
            while (index != INVALID_INDEX)
            {
                Animation* anim = &world->m_Animations[world->m_AnimMap[index]];
                if (anim->m_ComponentId == component_id && anim->m_PropertyId == property_id)
                {
                    StopAnimation(anim, false);
                }
                index = anim->m_Next;
            }
        }
    }

    static void StopAllAnimations(AnimWorld* world, uint16_t* head_ptr)
    {
        if (head_ptr != 0x0)
        {
            uint16_t index = *head_ptr;
            while (index != INVALID_INDEX)
            {
                Animation* anim = &world->m_Animations[world->m_AnimMap[index]];
                StopAnimation(anim, false);
                index = anim->m_Next;
            }
        }
    }

    UpdateResult CompAnimUpdate(const ComponentsUpdateParams& params)
    {
        /*
         * The update is divided into three passes.
         *
         * The first pass updates the delay and starts animations. Newly started
         * animations should automatically cancel others of the same property,
         * which is why this is done in a separate pass. Otherwise an ongoing
         * animation could have already animated the property and it would then
         * have an incorrect value when read by the newly started animation to
         * retrieve the from-value.
         *
         * The second pass advances and evaluates the animations.
         *
         * The third pass prunes stopped animations and call callbacks.
         *
         * The reason for this is to give consistent animation evaluation, independent of ordering.
         */
        UpdateResult result = UPDATE_RESULT_OK;
        AnimWorld* world = (AnimWorld*)params.m_World;
        world->m_InUpdate = 1;
        uint32_t size = world->m_Animations.Size();
        DM_COUNTER("animc", size);
        uint32_t i = 0;
        for (i = 0; i < size; ++i)
        {
            Animation& anim = world->m_Animations[i];
            if (!anim.m_Playing)
                continue;
            float dt = params.m_UpdateContext->m_DT;
            // Check delay
            if (anim.m_Delay > dt)
            {
                continue;
            }
            if (anim.m_FirstUpdate)
            {
                anim.m_FirstUpdate = 0;
                // Update from-value
                if (!anim.m_Composite)
                {
                    if (anim.m_Value != 0x0)
                        anim.m_From = *anim.m_Value;
                    else
                    {
                        PropertyDesc desc;
                        GetProperty(anim.m_Instance, anim.m_ComponentId, anim.m_PropertyId, desc);
                        anim.m_From = (float)desc.m_Variant.m_Number;
                    }
                }
                // Cancel other currently playing animations
                uint16_t* head_ptr = world->m_InstanceToIndex.Get((uintptr_t)anim.m_Instance);
                if (head_ptr != 0x0)
                {
                    uint16_t index = *head_ptr;
                    while (index != INVALID_INDEX)
                    {
                        uint16_t anim_index = world->m_AnimMap[index];
                        Animation* a2 = &world->m_Animations[anim_index];
                        if (anim_index != i && !a2->m_FirstUpdate && a2->m_ComponentId == anim.m_ComponentId
                                && a2->m_PropertyId == anim.m_PropertyId)
                        {
                            StopAnimation(a2, false);
                        }
                        index = a2->m_Next;
                    }
                }
            }
        }
        i = 0;
        for (i = 0; i < size; ++i)
        {
            Animation& anim = world->m_Animations[i];
            // Ignore canceled or delayed animations
            if (!anim.m_Playing)
                continue;
            float dt = params.m_UpdateContext->m_DT;
            if (anim.m_Delay > dt)
            {
                anim.m_Delay -= dt;
                continue;
            }
            // Take care of possible underflow
            dt -= anim.m_Delay;
            anim.m_Delay = 0.0f;
            // Reset delay
            anim.m_Delay = 0.0f;
            // Advance cursor
            if (anim.m_Playback != PLAYBACK_NONE)
            {
                anim.m_Cursor += dt;
            }
            // Adjust cursor
            bool completed = false;
            switch (anim.m_Playback)
            {
            case PLAYBACK_ONCE_FORWARD:
            case PLAYBACK_ONCE_BACKWARD:
                if (anim.m_Cursor >= anim.m_Duration)
                {
                    anim.m_Cursor = anim.m_Duration;
                    completed = true;
                }
                break;
            case PLAYBACK_LOOP_FORWARD:
            case PLAYBACK_LOOP_BACKWARD:
                while (anim.m_Cursor >= anim.m_Duration)
                {
                    anim.m_Cursor -= anim.m_Duration;
                }
                break;
            case PLAYBACK_LOOP_PINGPONG:
                while (anim.m_Cursor >= anim.m_Duration)
                {
                    anim.m_Cursor -= anim.m_Duration;
                    anim.m_Backwards = ~anim.m_Backwards;
                }
                break;
            default:
                break;
            }
            // Evaluate animation
            if (!anim.m_Composite)
            {
                float t = 1.0f;
                if (anim.m_Cursor < anim.m_Duration)
                    t = dmMath::Clamp(anim.m_Cursor * anim.m_InvDuration, 0.0f, 1.0f);
                if (anim.m_Backwards)
                    t = 1.0f - t;
                t = dmEasing::GetValue(anim.m_Easing, t);
                float v = anim.m_From + (anim.m_To - anim.m_From) * t;
                if (anim.m_Value != 0x0)
                {
                    *anim.m_Value = v;
                }
                else
                {
                    SetProperty(anim.m_Instance, anim.m_ComponentId, anim.m_PropertyId, PropertyVar(v));
                }
            }
            if (completed)
            {
                StopAnimation(&anim, true);
            }
        }
        i = 0;
        // Prune canceled animations and call callbacks
        while (i < size)
        {
            Animation* anim = &world->m_Animations[i];
            if (!anim->m_Playing)
            {
                if (anim->m_AnimationStopped != 0x0)
                {
                    uint32_t orig_size = size;
                    anim->m_AnimationStopped(anim->m_Instance, anim->m_ComponentId, anim->m_PropertyId, anim->m_Finished,
                            anim->m_Userdata1, anim->m_Userdata2);
                    // Check if the callback added animations, in which case we need to update the pointer (possible relocation)
                    size = world->m_Animations.Size();
                    if (size != orig_size)
                        anim = &world->m_Animations[i];
                }
                uint16_t* head_ptr = world->m_InstanceToIndex.Get((uintptr_t)anim->m_Instance);
                uint16_t* index_ptr = head_ptr;
                while (*index_ptr != INVALID_INDEX)
                {
                    if (*index_ptr == anim->m_Index)
                    {
                        *index_ptr = anim->m_Next;
                        world->m_AnimMapIndexPool.Push(anim->m_Index);
                        break;
                    }
                    else
                    {
                        index_ptr = &world->m_Animations[world->m_AnimMap[*index_ptr]].m_Next;
                    }
                }
                // Remove instance when the list is empty
                if (*head_ptr == INVALID_INDEX)
                {
                    world->m_InstanceToIndex.Erase((uintptr_t)anim->m_Instance);
                }
                // delete the instance from the list
                anim = &world->m_Animations.EraseSwap(i);
                --size;
                if (size > i)
                {
                    // We swapped, anim points to the swapped animation, update its map
                    world->m_AnimMap[anim->m_Index] = i;
                }
            }
            else
            {
                ++i;
            }
        }
        world->m_InUpdate = 0;
        return result;
    }

    static AnimWorld* GetWorld(HCollection collection)
    {
        uint32_t resource_type;
        dmResource::Result result = dmResource::GetTypeFromExtension(collection->m_Factory, "animc", &resource_type);
        assert(result == dmResource::RESULT_OK);
        uint32_t component_index;
        ComponentType* type = FindComponentType(collection->m_Register, resource_type, &component_index);
        assert(type != 0x0);
        return (AnimWorld*)collection->m_ComponentWorlds[component_index];
    }

    static bool PlayAnimation(AnimWorld* world, HInstance instance, dmhash_t component_id,
                     dmhash_t property_id,
                     Playback playback,
                     float* value,
                     float from,
                     float to,
                     dmEasing::Type easing,
                     float duration,
                     float delay,
                     AnimationStopped animation_stopped,
                     void* userdata1, void* userdata2,
                     bool composite)
    {
        uint32_t top = world->m_Animations.Size();
        if (top == MAX_CAPACITY)
        {
            dmLogError("Animation could not be stored since the buffer is full (%d).", MAX_CAPACITY);
            return false;
        }
        uint16_t index = world->m_AnimMapIndexPool.Pop();
        uint16_t* index_ptr = world->m_InstanceToIndex.Get((uintptr_t)instance);
        if (index_ptr == 0x0)
        {
            if (world->m_InstanceToIndex.Full())
            {
                dmLogError("Animation could not be stored since the instance buffer is full (%d).", world->m_InstanceToIndex.Size());
                world->m_AnimMapIndexPool.Push(index);
                return false;
            }
            world->m_InstanceToIndex.Put((uintptr_t)instance, index);
        }
        else
        {
            Animation* last_anim = &world->m_Animations[world->m_AnimMap[*index_ptr]];
            while (last_anim->m_Next != INVALID_INDEX)
            {
                last_anim = &world->m_Animations[world->m_AnimMap[last_anim->m_Next]];
            }
            last_anim->m_Next = index;
        }

        if (world->m_Animations.Full())
        {
            // Growth heuristic is to grow with the mean of MIN_CAPACITY_GROWTH and half current capacity, and at least MIN_CAPACITY_GROWTH
            uint32_t capacity = world->m_Animations.Capacity();
            uint32_t growth = dmMath::Min(MIN_CAPACITY_GROWTH, (MIN_CAPACITY_GROWTH + capacity / 2) / 2);
            capacity = dmMath::Min(capacity + growth, MAX_CAPACITY);
            world->m_Animations.SetCapacity(capacity);
        }
        uint32_t anim_count = top + 1;
        world->m_Animations.SetSize(anim_count);

        Animation& animation = world->m_Animations[top];
        memset(&animation, 0, sizeof(Animation));

        world->m_AnimMap[index] = top;
        animation.m_Index = index;

        animation.m_Instance = instance;
        animation.m_ComponentId = component_id;
        animation.m_PropertyId = property_id;
        animation.m_Playback = playback;
        animation.m_Easing = easing;
        animation.m_Value = value;
        animation.m_From = from;
        animation.m_To = to;
        animation.m_Delay = dmMath::Max(delay, 0.0f);
        animation.m_Duration = dmMath::Max(duration, 0.0f);
        animation.m_InvDuration = 0.0f;
        if (animation.m_Duration > 0.0f)
            animation.m_InvDuration = 1.0f / animation.m_Duration;
        animation.m_AnimationStopped = animation_stopped;
        animation.m_Userdata1 = userdata1;
        animation.m_Userdata2 = userdata2;
        animation.m_Next = INVALID_INDEX;
        animation.m_Playing = 1;
        animation.m_Composite = composite ? 1 : 0;
        if (animation.m_Playback == PLAYBACK_ONCE_BACKWARD || animation.m_Playback == PLAYBACK_LOOP_BACKWARD)
            animation.m_Backwards = 1;
        animation.m_FirstUpdate = 1;
        return true;
    }

    static bool PlayCompositeAnimation(AnimWorld* world, HInstance instance, dmhash_t component_id,
            dmhash_t property_id, Playback playback, float duration, AnimationStopped animation_stopped,
            void* userdata1, void* userdata2)
    {
        return PlayAnimation(world, instance, component_id, property_id, playback, 0x0, 0, 0, dmEasing::TYPE_LINEAR,
                duration, 0, animation_stopped, userdata1, userdata2, true);
    }

    static uint32_t GetElementCount(PropertyType type)
    {
        switch (type)
        {
        case PROPERTY_TYPE_NUMBER:
            return 1;
        case PROPERTY_TYPE_VECTOR3:
            return 3;
        case PROPERTY_TYPE_VECTOR4:
        case PROPERTY_TYPE_QUAT:
            return 4;
        default:
            return 0;
        }
    }

    PropertyResult Animate(HCollection collection, HInstance instance, dmhash_t component_id,
                     dmhash_t property_id,
                     Playback playback,
                     const PropertyVar& to,
                     dmEasing::Type easing,
                     float duration,
                     float delay,
                     AnimationStopped animation_stopped,
                     void* userdata1, void* userdata2)
    {
        if (instance == 0)
            return PROPERTY_RESULT_INVALID_INSTANCE;
        PropertyDesc prop_desc;
        PropertyResult prop_result = GetProperty(instance, component_id, property_id, prop_desc);
        if (prop_result != PROPERTY_RESULT_OK)
        {
            return prop_result;
        }
        if (to.m_Type != prop_desc.m_Variant.m_Type)
        {
            return PROPERTY_RESULT_TYPE_MISMATCH;
        }
        uint32_t element_count = GetElementCount(prop_desc.m_Variant.m_Type);
        if (element_count == 0)
        {
            return PROPERTY_RESULT_UNSUPPORTED_TYPE;
        }
        AnimWorld* world = GetWorld(collection);

        if (element_count > 1)
        {
            if (!PlayCompositeAnimation(world, instance, component_id, property_id, playback,
                    duration, animation_stopped, userdata1, userdata2))
                return PROPERTY_RESULT_BUFFER_OVERFLOW;
            float* v = prop_desc.m_Variant.m_V4;
            for (uint32_t i = 0; i < element_count; ++i)
            {
                float* val_ptr = 0x0;
                if (prop_desc.m_ValuePtr != 0x0)
                    val_ptr = prop_desc.m_ValuePtr + i;
                if (!PlayAnimation(world, instance, component_id, prop_desc.m_ElementIds[i], playback, val_ptr,
                        *(v + i), to.m_V4[i], easing, duration, delay, 0x0, 0x0, 0x0, false))
                    return PROPERTY_RESULT_BUFFER_OVERFLOW;
            }
        }
        else
        {
            if (!PlayAnimation(world, instance, component_id, property_id, playback, prop_desc.m_ValuePtr,
                    (float)prop_desc.m_Variant.m_Number, (float)to.m_Number, easing, duration, delay, animation_stopped,
                    userdata1, userdata2, false))
                return PROPERTY_RESULT_BUFFER_OVERFLOW;
        }
        return PROPERTY_RESULT_OK;
    }

    PropertyResult CancelAnimations(HCollection collection, HInstance instance, dmhash_t component_id,
                     dmhash_t property_id)
    {
        if (instance == 0)
            return PROPERTY_RESULT_INVALID_INSTANCE;
        PropertyDesc prop_desc;
        PropertyResult prop_result = GetProperty(instance, component_id, property_id, prop_desc);
        if (prop_result != PROPERTY_RESULT_OK)
        {
            return prop_result;
        }
        uint32_t element_count = GetElementCount(prop_desc.m_Variant.m_Type);
        if (element_count == 0)
        {
            return PROPERTY_RESULT_UNSUPPORTED_TYPE;
        }
        AnimWorld* world = GetWorld(collection);
        uint16_t* head_ptr = world->m_InstanceToIndex.Get((uintptr_t)instance);
        StopAnimations(world, head_ptr, component_id, property_id);
        if (element_count > 1)
        {
            for (uint32_t i = 0; i < element_count; ++i)
            {
                StopAnimations(world, head_ptr, component_id, prop_desc.m_ElementIds[i]);
            }
        }
        return PROPERTY_RESULT_OK;
    }

    void CancelAnimations(HCollection collection, HInstance instance)
    {
        AnimWorld* world = GetWorld(collection);
        // Deferred cancel while in update
        if (world->m_InUpdate)
        {
            StopAllAnimations(world, world->m_InstanceToIndex.Get((uintptr_t)instance));
        }
        else
        {
            uint16_t* head_ptr = world->m_InstanceToIndex.Get((uintptr_t)instance);
            if (head_ptr != 0x0)
            {
                uint32_t anim_count = world->m_Animations.Size();
                uint16_t index = *head_ptr;
                while (index != INVALID_INDEX)
                {
                    uint16_t anim_index = world->m_AnimMap[index];
                    Animation* anim = &world->m_Animations[anim_index];
                    StopAnimation(anim, false);
                    if (anim->m_AnimationStopped != 0x0)
                    {
                        anim->m_AnimationStopped(anim->m_Instance, anim->m_ComponentId, anim->m_PropertyId, anim->m_Finished,
                                anim->m_Userdata1, anim->m_Userdata2);
                    }
                    world->m_AnimMapIndexPool.Push(index);
                    index = anim->m_Next;
                    // delete the instance from the list
                    anim_index = (uint16_t)(anim - world->m_Animations.Begin());
                    anim = &world->m_Animations.EraseSwap(anim_index);
                    --anim_count;
                    if (anim_count > anim_index)
                    {
                        // We swapped, anim points to the swapped animation, update its map
                        world->m_AnimMap[anim->m_Index] = anim_index;
                    }
                }
                world->m_InstanceToIndex.Erase((uintptr_t)instance);
            }
        }
    }
}
