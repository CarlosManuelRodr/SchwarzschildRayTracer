#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <SFML/Graphics.hpp>
#include "Sphere.h"
#include "HitableList.h"
#include "Camera.h"
#include "RandomGen.h"
#include "Material.h"
#include "ThreadPool.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include "stb_image.h"

using namespace std;

Vector3 color_from_ray(const Ray& r, HitableList world, int depth)
{
    HitRecord rec;
    if (world.Hit(r, 0.001f, FLT_MAX, rec))
    {
        Ray scattered;
        Vector3 attenuation;
        Vector3 emitted = rec.matPtr->Emitted(rec.u, rec.v, rec.p);

        if (depth < 50 && rec.matPtr->Scatter(r, rec, attenuation, scattered))
        {
            return emitted + attenuation * color_from_ray(scattered, world, depth + 1);
        }
        else
        {
            return unit_vector(emitted);
        }
    }
    else
    {
        Vector3 unitDirection = unit_vector(r.Direction());
        float t = 0.5f * (unitDirection.y() + 1.0f);
        return (1.0f - t) * Vector3(1.0f, 1.0f, 1.0f) + t * Vector3(0.5f, 0.7f, 1.0f);
    }
}

vector<sf::Color> render_world_line(int j, int nx, int ny, int ns, const HitableList& world, const Camera& camera)
{
    vector<sf::Color> output;
    for (int i = 0; i < nx; i++)
    {
        Vector3 rayColor(0.0, 0.0, 0.0);
        for (int s = 0; s < ns; s++)
        {
            float u = float(i + RandomGen::Getfloat()) / float(nx);
            float v = float(j + RandomGen::Getfloat()) / float(ny);
            Ray r = camera.GetRay(u, v);
            rayColor += color_from_ray(r, world, 0);
        }
        rayColor /= float(ns);
        rayColor = Vector3(sqrt(rayColor[0]), sqrt(rayColor[1]), sqrt(rayColor[2]));

        int ir = int(255.99 * rayColor[0]);
        int ig = int(255.99 * rayColor[1]);
        int ib = int(255.99 * rayColor[2]);
        output.push_back(sf::Color(ir, ig, ib));
    }
    return output;
}

sf::Image bitmap_to_image(vector< vector<sf::Color> > imageAsBitmap)
{
    int ny = (int) imageAsBitmap.size();
    int nx = (int) imageAsBitmap.front().size();

    sf::Image output;
    output.create(nx, ny);

    for (int j = 0; j < ny; j++)
    {
        for (int i = 0; i < nx; i++)
        {
            output.setPixel(i, ny - j - 1, imageAsBitmap[j][i]);
        }
    }

    return output;
}

sf::Image render_world_mt(int nx, int ny, int ns, const HitableList& world, const Camera& camera)
{
    unsigned int hc = thread::hardware_concurrency();

    ThreadPool pool(hc);
    vector<future<vector<sf::Color>>> result;
    vector<vector<sf::Color>> imageAsBitmap;

    for (int j = 0; j < ny; j++)
        result.push_back(pool.enqueue(render_world_line, j, nx, ny, ns, world, camera));

    for (auto& r : result)
        imageAsBitmap.push_back(r.get());

    return bitmap_to_image(imageAsBitmap);
}

sf::Image render_world_st(int nx, int ny, int ns, const HitableList& world, const Camera& camera)
{
    vector<vector<sf::Color>> imageAsBitmap;

    for (int j = 0; j < ny; j++)
        imageAsBitmap.push_back(render_world_line(j, nx, ny, ns, world, camera));


    return bitmap_to_image(imageAsBitmap);
}

int main()
{
    RandomGen::Seed();
    int nx = 800;
    int ny = 600;
    int ns = 50;

    // Load textures
    int earthTextWidth, earthTextHeight, earthTextChannels;
    unsigned char *earthTexture = stbi_load("Textures\\earthmap.jpg", &earthTextWidth, &earthTextHeight, &earthTextChannels, 0);
    int skyTextWidth, skyTextHeight, skyTextChannels;
    unsigned char* skyTexture = stbi_load("Textures\\starbackground.jpg", &skyTextWidth, &skyTextHeight, &skyTextChannels, 0);

    // Set-up world
    vector<Hitable*> hitableList;
    hitableList.push_back(
        new Sphere(
            Vector3(7.0f, 0.0f, -1.0f), 1.0f,
            new Lambertian(new ImageTexture(earthTexture, earthTextWidth, earthTextHeight))
        )
    );
    hitableList.push_back(
        new Sphere(
            Vector3(7.0f, 2.5f, -1.0f), 0.5f,
            new DiffuseLight(new ConstantTexture(Vector3(2.0f, 2.0f, 2.0f)))
        )
    );
    hitableList.push_back(
        new Sphere(
            Vector3(0.0f, 0.0f, -1.0f), 5.5f,
            new Schwarzschild(0.01f)
        )
    );
    hitableList.push_back(
        new Sphere(
            Vector3(0.0f, 0.0f, 0.0f), 200.0f,
            new Lambertian(new ImageTexture(skyTexture, skyTextWidth, skyTextHeight))
        )
    );
    HitableList world(hitableList);

    // Camera set-up
    Vector3 cameraPosition(4.0f, 7.0f, 3.0f);
    Vector3 cameraLookAt(4.0f, 0.0f, -1.0f);
    Camera camera = Camera(cameraPosition, cameraLookAt, Vector3(0.0f, 1.0f, 0.0f), 90.0f, (float) nx / (float) ny);

    // Initialize window
    sf::RenderWindow window(sf::VideoMode(nx, ny), "Ray Tracer");

    sf::Texture texture;
    texture.create(nx, ny);
    sf::Sprite sprite;
    sprite.setTexture(texture);

#if defined(_DEBUG)
    texture.update(render_world_st(nx, ny, ns, world, camera));
#else
    texture.update(render_world_mt(nx, ny, ns, world, camera));
#endif

    const float movementDelta = 0.1f;
    while (window.isOpen())
    {
        sf::Event event;
        while (window.pollEvent(event))
        {
            if (event.type == sf::Event::Closed)
                window.close();
            if (event.type == sf::Event::KeyPressed)
            {
                // Directional keys
                if (event.key.code == sf::Keyboard::Right)
                {
                    cameraPosition += Vector3(movementDelta, 0.0f, 0.0f);
                    cameraLookAt += Vector3(movementDelta, 0.0f, 0.0f);
                }
                if (event.key.code == sf::Keyboard::Left)
                {
                    cameraPosition += Vector3(-movementDelta, 0.0f, 0.0f);
                    cameraLookAt += Vector3(-movementDelta, 0.0f, 0.0f);
                }
                if (event.key.code == sf::Keyboard::Up)
                {
                    cameraPosition += Vector3(0.0f, 0.0f, -movementDelta);
                    cameraLookAt += Vector3(0.0f, 0.0f, -movementDelta);
                }
                if (event.key.code == sf::Keyboard::Down)
                {
                    cameraPosition += Vector3(0.0f, 0.0f, movementDelta);
                    cameraLookAt += Vector3(0.0f, 0.0f, movementDelta);
                }
                if (event.key.code == sf::Keyboard::Q)
                {
                    cameraPosition += Vector3(0.0f, movementDelta, 0.0f);
                    cameraLookAt += Vector3(0.0f, movementDelta, 0.0f);
                }
                if (event.key.code == sf::Keyboard::E)
                {
                    cameraPosition += Vector3(0.0f, -movementDelta, 0.0f);
                    cameraLookAt += Vector3(0.0f, -movementDelta, 0.0f);
                }

                // WASD
                if (event.key.code == sf::Keyboard::D)
                    cameraLookAt += Vector3(movementDelta, 0.0f, 0.0f);
                if (event.key.code == sf::Keyboard::A)
                    cameraLookAt += Vector3(-movementDelta, 0.0f, 0.0f);
                if (event.key.code == sf::Keyboard::W)
                    cameraLookAt += Vector3(0.0f, 0.0f, -movementDelta);
                if (event.key.code == sf::Keyboard::S)
                    cameraLookAt += Vector3(0.0f, 0.0f, movementDelta);

                camera.SetLookFrom(cameraPosition);
                camera.SetLookAt(cameraLookAt);

            #if defined(_DEBUG)
                texture.update(render_world_st(nx, ny, ns, world, camera));
            #else
                texture.update(render_world_mt(nx, ny, ns, world, camera));
            #endif
            }
        }

        window.clear();
        window.draw(sprite);
        window.display();
    }
}