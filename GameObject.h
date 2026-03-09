	#ifndef GAMEOBJECT_CLASS_H
	#define GAMEOBJECT_CLASS_H
	#include "Mesh.h"
	#include "Light.h"
	#include "Transform.h"
	#include "MeshRenderer.h"
	class GameObject {
	public:
		GameObject(){}
		std::string name = "New Object";
		std::string modelPath = "";
		std::vector<std::string> materialPaths;
		bool isStatic = false;
		bool isVisible = true;
		bool castShadow = true;
		std::vector<std::string> tags;
		std::vector<std::string> layers;
		Transform transform;
		MeshRenderer renderer;
		Light light;
	};
	#endif