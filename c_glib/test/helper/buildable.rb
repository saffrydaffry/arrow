# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

module Helper
  module Buildable
    def build_boolean_array(values)
      build_array(Arrow::BooleanArrayBuilder, values)
    end

    def build_int_array(values)
      build_array(Arrow::IntArrayBuilder, values)
    end

    def build_int8_array(values)
      build_array(Arrow::Int8ArrayBuilder, values)
    end

    def build_uint8_array(values)
      build_array(Arrow::UInt8ArrayBuilder, values)
    end

    def build_int16_array(values)
      build_array(Arrow::Int16ArrayBuilder, values)
    end

    def build_uint16_array(values)
      build_array(Arrow::UInt16ArrayBuilder, values)
    end

    def build_int32_array(values)
      build_array(Arrow::Int32ArrayBuilder, values)
    end

    def build_uint32_array(values)
      build_array(Arrow::UInt32ArrayBuilder, values)
    end

    def build_int64_array(values)
      build_array(Arrow::Int64ArrayBuilder, values)
    end

    def build_uint64_array(values)
      build_array(Arrow::UInt64ArrayBuilder, values)
    end

    def build_float_array(values)
      build_array(Arrow::FloatArrayBuilder, values)
    end

    def build_double_array(values)
      build_array(Arrow::DoubleArrayBuilder, values)
    end

    def build_date32_array(values)
      build_array(Arrow::Date32ArrayBuilder, values)
    end

    def build_date64_array(values)
      build_array(Arrow::Date64ArrayBuilder, values)
    end

    def build_binary_array(values)
      build_array(Arrow::BinaryArrayBuilder, values)
    end

    def build_string_array(values)
      build_array(Arrow::StringArrayBuilder, values)
    end

    def build_list_array(value_data_type, values_list)
      value_field = Arrow::Field.new("value", value_data_type)
      data_type = Arrow::ListDataType.new(value_field)
      builder = Arrow::ListArrayBuilder.new(data_type)
      value_builder = builder.value_builder
      values_list.each do |values|
        if values.nil?
          builder.append_null
        else
          builder.append
          values.each do |value|
            if value.nil?
              value_builder.append_null
            else
              value_builder.append(value)
            end
          end
        end
      end
      builder.finish
    end

    def build_struct_array(fields, structs)
      data_type = Arrow::StructDataType.new(fields)
      builder = Arrow::StructArrayBuilder.new(data_type)
      structs.each do |struct|
        if struct.nil?
          builder.append_null
        else
          builder.append
          struct.each do |name, value|
            field_builder_index = fields.index {|field| field.name == name}
            field_builder = builder.get_field_builder(field_builder_index)
            if value.nil?
              field_builder.append_null
            else
              field_builder.append(value)
            end
          end
        end
      end
      builder.finish
    end

    private
    def build_array(builder_class, values)
      builder = builder_class.new
      values.each do |value|
        if value.nil?
          builder.append_null
        else
          builder.append(value)
        end
      end
      builder.finish
    end
  end
end
