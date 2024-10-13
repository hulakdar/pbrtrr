#pragma once
#include <wrl/client.h>

template <typename T>
struct TComPtr
{
    TComPtr() : Ptr(nullptr) { }
    TComPtr(T *In)
    {
        Ptr = In;
        if (Ptr)
            Ptr->AddRef();
    }

    TComPtr(const TComPtr<T>& In)
    {
        *this = In;
    }
    TComPtr(TComPtr<T>&& In)
    {
        *this = MOVE(In);
    }
    ~TComPtr()
    {
        Reset();
    }

    void Reset()
    {
        if (Ptr)
            Ptr->Release();
        Ptr = nullptr;
    }

    TComPtr<T>& operator=(const TComPtr<T>& Other)
    {
        Reset();
        Ptr = Other.Ptr;
        if (Ptr)
        {
            Ptr->AddRef();
        }
        return *this;
    }

    TComPtr<T>& operator=(TComPtr<T>&& Other)
    {
        std::swap(Other.Ptr, Ptr);
        return *this;
    }

    template <typename TT>
    long As(TT** Out) {return Ptr->QueryInterface(__uuidof(TT), (void**)Out);}

    T** GetAddressOf() {return (T**)&Ptr;}
    T** operator&() {return (T**)&Ptr;}

    operator bool() const { return Ptr != nullptr; }
    T* operator->() const { return (T*)Ptr; }
    operator T*() const { return (T*)Ptr; }
    T* Get() const { return (T*)Ptr; }
    IUnknown* Ptr = nullptr;
};
