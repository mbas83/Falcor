#pragma once
#include <vector>

/*

    {1.,0.,0.},       //red
    {1.,1.,0.},       //yellow
    {0.,1.,0.},       //green
    {0.25,0.58,0.7},  //light blue
    {0.05,0.07,0.35}, //dark blue
    {0.5,0.07,0.3}    //violet
    */

static std::vector<std::vector<UINT8>> mipColors =
{
    {0xFF,0x00,0x00}, //red
    {0xFF,0xFF,0x00}, //yellow
    {0x00,0xFF,0x00}, //green
    {0x86,0xC9,0xDA}, //light blue
    {0x42,0x4B,0x99}, //dark blue
    {0xBC,0x4A,0x94}, //violet
    {0xFF,0xFF,0xFF}  //white
};

static std::vector<UINT8> getMipColorData()
{
    std::vector<UINT8> data;
    data.reserve(7 * 4);

    for (const auto& elem : mipColors)
    {
        for (const auto& color : elem)
        {
            data.emplace_back(color);
        }
        // alpha = 1
        data.emplace_back(0xFF);
    }

    return data;
}
