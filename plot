#!/usr/bin/env python3

import sys

def draw_pair(l, r):
    symbols = {
        "00": " ",

        "10": "⡀",
        "11": "⣀",
        "01": "⢀",

        "20": "⡄",
        "21": "⣄",
        "22": "⣤",
        "12": "⣠",
        "02": "⢠",

        "30": "⡆",
        "31": "⣆",
        "32": "⣦",
        "33": "⣶",
        "23": "⣴",
        "13": "⣰",
        "03": "⢰",

        "40": "⡇",
        "41": "⣇",
        "42": "⣧",
        "43": "⣷",
        "44": "⣿",
        "34": "⣾",
        "24": "⣼",
        "14": "⣸",
        "04": "⢸",
    }

    key = str(l)+str(r)

    return symbols[key]

def topmedbot(v):
    if v <= 4:
        return 0, 0, v

    if v <= 8:
        med = v - 4
        bot = 4
        return 0, med, bot

    top = v - 8
    med = 4
    bot = 4
    return top, med, bot


def draw_bars(l, r):
    ltop, lmed, lbot = topmedbot(l)
    rtop, rmed, rbot = topmedbot(r)

    return [draw_pair(ltop, rtop), draw_pair(lmed, rmed), draw_pair(lbot, rbot)]

def map_uniform(v, f, t):
    return int((v - f[0]) * (t[1] - t[0]) / (f[1] - f[0]) + t[0])

def draw(vals):
    top = []
    med = []
    bot = []
    leg = []

    i = 0
    for l, r in zip(*[iter(vals)]*2):
        bars = draw_bars(l, r)
        top.append(bars[0])
        med.append(bars[1])
        bot.append(bars[2])

        if i % 4 == 0:
            t = i * 2
            leg.append(f"{t:<4}")

        i += 1

    print("".join(top))
    print("".join(med))
    print("".join(bot))
    print("".join(leg))

def draw_points(name, points):
    print(f"{name}:")

    if len(points) % 2 == 1:
        points.append(0)

    mx = max(points)
    vals = [map_uniform(x, (0, mx), (1, 12)) for x in points]
    draw(vals)

for line in sys.stdin:
    line = line.strip()
    points = line.split(" ")
    name = points[0]
    points.pop(0)
    points = [int(x) for x in points]
    draw_points(name, points)
