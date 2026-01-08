	#ifndef GAMEOBJECT_CLASS_H
	#define GAMEOBJECT_CLASS_H
	#include "Mesh.h"
	#include "Light.h"
	#include "Transform.h"

	class GameObject {
	public:
		GameObject(){}
		Transform transform;
		Mesh* mesh;
		Material* material;
		Light light;
	};
	#endif