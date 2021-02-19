/*
 * Copyright (C) 2021 Oleksiy Lukin alukin@gmail.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
package ua.cn.al.s7.s7core;

import java.util.ArrayList;
import java.util.List;

/**
 *
 * @author Oleksiy Lukin alukin@gmail.com
 */
public class Schema extends Element {

    List<Element> elements = new ArrayList<>();

    public boolean verify() {
        boolean res = true;
        //TODO: all unconnected inputs should be "grounded"
        // graph should be consistent
        return res;
    }

    @Override
    public void tick() {
        //TODO
    }

    public void insert(Element e) {
        elements.add(e);
    }

    public void connect(Output out, Input in) {
        in.connect(out);
    }

    public void runTicks(int nTicks) {
        //run ticks in graph iterator order
        for (int i = 0; i < nTicks; i++) {
            for (Element e : elements) {
                e.tick();
            }
        }
    }
}
