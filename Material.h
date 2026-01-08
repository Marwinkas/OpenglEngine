#ifndef MATERIAL_CLASS_H
#define MATERIAL_CLASS_H

#include<string>

#include"VAO.h"
#include"EBO.h"
#include"Camera.h"
#include"Texture.h"
#include "shaderClass.h"
static int MaterialID;
class Material
{
public:
	int ID;
	Material() { ID = MaterialID; MaterialID++; };
	Texture albedo;
	Texture normal;
	Texture height;
	Texture metallic;
	Texture roughness;
	Texture ao;

	bool hasalbedo ;
	bool hasnormal;
	bool hasheight;
	bool hasmetallic;
	bool hasroughness;
	bool hasao;

	void setAlbedo(std::string path);
	void setNormal(std::string path);
	void setHeight(std::string path);
	void setMetallic(std::string path);
	void setRoughness(std::string path);
	void setAO(std::string path);
	void Activate(Shader& shader);

};
#endif