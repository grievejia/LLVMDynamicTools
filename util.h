#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

// Smart pointer that does auto memory management using reference count
template <class T>
class RefCountPtr
{
private:
	T* pointee;
	void init() { if (pointee != NULL) pointee->addReference(); }
public:
	RefCountPtr(T* realPtr = NULL): pointee(realPtr) { init(); }
	RefCountPtr(const RefCountPtr& other): pointee(other.pointee) { init(); }
	~RefCountPtr() { if (pointee != NULL) pointee->removeReference(); }
	
	RefCountPtr& operator=(const RefCountPtr& rhs)
	{
		if (pointee != rhs.pointee)
		{
			T* oldPointee = pointee;
			pointee = rhs.pointee;
			init();
			if (oldPointee != NULL)
				oldPointee->removeReference();
		}
		return *this;
	}
	bool operator ==(const RefCountPtr& other) const { return pointee == other.pointee; }
	bool operator !=(const RefCountPtr& other) const { return !(*this == other); }
	bool isNull() { return pointee == NULL; }
	
	T* operator->() const { return pointee; }
	T& operator*() const { return *pointee; }
	const T* get() const { return pointee; }
};

class RefCountObject
{
private:
	unsigned refCount;
public:
	RefCountObject(): refCount(0) {}
	RefCountObject(const RefCountObject& other): refCount(0) {}
	virtual ~RefCountObject() = 0;
	
	RefCountObject& operator=(const RefCountObject& rhs) { return *this; }
	
	void addReference() { ++refCount; }
	void removeReference() { if (--refCount == 0) delete this; }
	bool isShared() const { return refCount > 1; }
};

#endif
