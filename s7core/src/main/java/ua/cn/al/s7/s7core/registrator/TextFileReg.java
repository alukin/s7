/*
 * Copyright (C) 2021 Oleksiy Lukin alukin@mail.com
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
package ua.cn.al.s7.s7core.registrator;

import java.io.BufferedWriter;
import java.io.Closeable;
import java.io.IOException;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.io.Writer;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import ua.cn.al.s7.s7core.Element;

/**
 *
 * @author Oleksiy Lukin alukin@mail.com
 */
@Slf4j
public class TextFileReg extends Element implements Closeable{
    @Getter
    private final int nChannels;
    private final Writer fileWtriter;
    
    public TextFileReg(int nChannels, OutputStream ostream) throws IOException {
        super(nChannels,0);
        this.nChannels=nChannels;
        fileWtriter = new BufferedWriter(new OutputStreamWriter(ostream));
    }

    @Override
    public void tick() {
        StringBuffer sb;
        sb = new StringBuffer(ownerSchema.getSchemaTime().toString());
        for(int i=0;i<nChannels;i++){
            sb.append("\t");
            sb.append(inputs.get(i).value().getDouble().toString());            
        }
        sb.append("\n");
        try {
            fileWtriter.write(sb.toString());
        } catch (IOException ex) {
            log.error("Can not write to file", ex);
        }
    }

    @Override
    public void close() throws IOException {
        try (fileWtriter) {
            fileWtriter.flush();
        }
    }
    
}
